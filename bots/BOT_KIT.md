# Bot kit — plan and specification

Status: proposed, not implemented.

A support library that bots compile against, providing the primitives a real
strategy needs. Today a bot gets `bot_api.h` and nothing else — `evaluate7()`,
the exact equity calculator, and the card helpers are all C++ internals of
`run_match`, and the 169-bucket mapping exists only in Python inside
`scripts/finalize_match.py`. Every strategy bot therefore re-derives everything
from raw `uint8` cards.

## Two properties that shape the design

**The kit is an API, not an ABI.** It is compiled into each bot rather than
called across the `dlopen` boundary, so changing it never invalidates an existing
compiled bot and never requires a `FELT_BOT_ABI_VERSION` bump. It can evolve far
more freely than `bot_api.h`. Bots pick up changes by rebuilding.

**Bots and the harness must agree on hand strength.** If a bot evaluates
showdowns with a different library than the harness settles them with, any
disagreement in ranking becomes a silent strategy bug. The kit therefore reuses
the harness's already-vendored OMPEval rather than introducing a second
evaluator.

## Sourcing policy

Prefer vendored, permissively licensed work over writing our own. In practice
this splits cleanly:

- **Evaluation, equity and range parsing: reuse OMPEval** (ISC, already vendored
  in evaluator-only form at `harness/third_party/ompeval`). Extending that
  vendoring costs nothing in new dependencies and keeps bot and harness in
  agreement.
- **Hand reading, board texture and Felt context: write it.** No permissively
  licensed C or C++ library provides made-hand classification *relative to the
  board*, draw detection, or board texture as reusable primitives. Evaluators
  return a five-card category, not "top pair with a flush draw". The surveyed
  alternatives (mkpoker, oopoker, PokerSource) are frameworks or carry
  unsuitable licenses.
- **Tables: generate, do not transcribe.** Anything derivable from the evaluator
  is produced by a checked-in generator and pinned with a golden test, rather
  than copied from a published chart whose provenance and exact definition we
  cannot verify.

## What to include

### Tier 1 — context and arithmetic

No card logic; needed in essentially every decision. All bespoke, all trivial,
all currently duplicated by hand in every bot.

| Primitive | Notes |
|---|---|
| `felt_pot_odds(state)` | required equity to call, `to_call / (pot + to_call)`. Error-prone because `pot` already includes committed chips |
| `felt_spr(state)` | stack-to-pot ratio; drives commitment decisions |
| `felt_effective_stack_bb(state, bb)` | depth in big blinds |
| `felt_raise_to_fraction(state, f)` | an `amount_to` for an f-of-pot raise, already clamped and short-all-in safe. The single highest-value helper, because this is where the total-versus-increment trap lives |
| `felt_preflop_raise_count(state)` | voluntary raises so far |
| `felt_pot_class(state)` | walk / limped / single-raised / 3-bet / 4-bet+, matching the definitions already used by the statistics |
| `felt_was_preflop_aggressor(state)` | required for any c-bet logic |
| `felt_street_bet_count(state)` | bets and raises faced on this street |
| `felt_checked_to_me(state)` | first-in or checked-to |

The history helpers matter more than they look: parsing `state->history` is
fiddly, and without them every bot reimplements the same loop slightly
differently, which makes bots incomparable.

### Tier 2 — hand reading

Pure rank-and-suit counting. No evaluator dependency, so this tier can stay
header-only.

**Preflop strength.** A 169-entry table mapping canonical bucket to cumulative
**combo-weighted** percentile, ordered by all-in equity against a uniformly
random hand.

```c
uint32_t felt_preflop_bucket(FeltCard a, FeltCard b);      /* 0..168 canonical */
uint32_t felt_preflop_percentile(FeltCard a, FeltCard b);  /* 0..1000 */
```

Combo weighting is not optional. There are 169 buckets but 1,326 combos — AA is
6 combos, AKs 4, AKo 12 — so "top 10% of hands" means 10% of the combos actually
dealt (about 133), not the top 17 buckets. Ranking by bucket produces badly wrong
thresholds.

Generated offline by an OMPEval-based tool, pinned as a golden table.

**Made hand and draws — two independent axes, not one.** "Air / weak draw / good
draw / top pair" conflates what you *have* with what you can *become*, and they
are orthogonal: top pair with a flush draw plays nothing like top pair.

```c
typedef struct {
  uint8_t made;           /* AIR, UNDERPAIR, BOTTOM_PAIR, MIDDLE_PAIR, TOP_PAIR,
                             OVERPAIR, TWO_PAIR, TRIPS, SET, STRAIGHT, FLUSH,
                             FULL_HOUSE, QUADS, STRAIGHT_FLUSH */
  uint8_t pair_kicker;    /* meaningful for one-pair hands */
  uint8_t draw;           /* NONE, BACKDOOR_FLUSH, GUTSHOT, OESD, FLUSH_DRAW,
                             COMBO_DRAW */
  uint8_t outs;           /* estimated clean outs */
  bool    nut_flush_draw;
  bool    uses_both_hole; /* separates a set from trips; supports blocker logic */
} FeltHandFeatures;

FeltHandFeatures felt_hand_features(const FeltCard *hole,
                                    const FeltCard *board,
                                    uint8_t board_count);
```

Note that "top pair" is *relative* — it requires comparing the pair's rank
against the board's ranks, which no evaluator exposes. Set versus trips matters
strategically and is likewise invisible to a plain evaluator.

**Board texture**, since the same made hand means different things on different
boards:

```c
typedef struct {
  bool paired, monotone, two_tone, rainbow;
  uint8_t high_rank, connectedness;   /* straight-completing potential */
} FeltBoardTexture;
```

### Tier 3 — equity

Needs a real evaluator, so it determines the packaging decision below. This is
where OMPEval earns its place: its `EquityCalculator` already provides range
versus range equity, Equilab-style range notation (`"QQ+,AKs,AcQc"`), both Monte
Carlo and exact enumeration, and preflop suit isomorphism.

```c
double felt_equity_vs_random(const FeltCard *hole, const FeltCard *board,
                             uint8_t board_count, uint32_t samples,
                             uint64_t *rng);
double felt_equity_vs_range(const FeltCard *hole, const FeltCard *board,
                            uint8_t board_count, const char *range,
                            uint32_t samples, uint64_t *rng);
```

Three constraints:

- **Force single-threaded.** OMPEval multithreads by default. Bots are
  single-threaded by contract, and worker threads would escape the
  `CLOCK_THREAD_CPUTIME_ID` measurement entirely.
- **Seed from `decision_random`.** Never a global or a clock, or replay and
  duplicate symmetry both break.
- **Warm at load.** OMPEval reports roughly 10 ms of initialization against a
  2 ms decision cap, so the first timed decision must not pay it. Initialize
  from `felt_bot_name()`, which the harness calls once at load outside any
  decision.

Cost in play is not a concern: 1,000 Monte Carlo samples is about 2,000
evaluations, on the order of 10 µs against a 2 ms cap.

## Packaging

Tiers 1 and 2 are header-only C at `harness/include/felt/bot_kit.h`, needing no
link step and keeping a bot a single self-contained source file.

Tier 3 cannot be, because OMPEval is C++. It becomes a static
`libfelt_botkit.a` with a C wrapper, and `add_felt_bot` links it. A bot that
never calls equity functions pays nothing.

If a strictly header-only kit ever becomes a requirement,
[phevaluator](https://github.com/HenryRLee/PokerHandEvaluator) (Apache 2.0) has
a genuine C API and ~100 KB of tables at 56-72 M hands/s. It is the fallback, not
the default, precisely because a second evaluator reintroduces the risk of
disagreeing with the harness about who won.

## Generated tables

One tool, `tools/gen_bot_tables`, emits C arrays into the kit:

- 169-entry preflop percentile table, combo-weighted, from OMPEval;
- the canonical 169-bucket index and its labels.

Both pinned by golden tests. The generator is checked in; the generated header
is committed so bots build without running it.

## Risks

**Bucket-mapping drift.** The 169-bucket mapping would then exist twice: C in the
kit and Python in `finalize_match.py:bucket_label()`. If they diverge, a bot's
idea of `76s` stops matching the statistics' idea, and per-bucket profitability
tables quietly describe something else. Cross-test both over all 1,326 combos, or
generate both from one source.

**Definition drift with the statistics.** `felt_pot_class` and
`felt_was_preflop_aggressor` must match the definitions in SPEC.md exactly, or a
bot's self-model will not match its own reported statistics.

**Licence.** OMPEval is ISC but bundles libdivide under its own terms; confirm
and record both before extending the vendored subset, as was done for the
evaluator-only portion.

**Timing.** Every kit call is charged to the bot's CPU cap. The kit should be
honest about cost: table lookups are free, `felt_hand_features` is cheap, equity
is not.

## Build order

1. Tier 1, with tests. Immediately useful and unblocks a real strategy bot.
2. Tier 2 hand features and texture, with scripted board tests.
3. The percentile generator and its golden table.
4. A `tight_aggressive` reference bot built entirely on Tiers 1-2, so the
   primitives are exercised rather than merely existing.
5. Tier 3 equity, once a bot actually needs it.

## Sources

- [OMPEval](https://github.com/zekyll/OMPEval) — ISC; evaluator and equity
  calculator with range notation, already vendored in part
- [PokerHandEvaluator / phevaluator](https://github.com/HenryRLee/PokerHandEvaluator)
  — Apache 2.0; C API fallback if header-only ever becomes a hard requirement

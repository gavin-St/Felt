# Bot kit and first postflop bots — implementation plan

Status: in progress. The made-hand, live-draw, and board-texture classifiers,
legal action helpers, built-in 100 bb preflop baseline, and first three
postflop bots are implemented. Range-strength and equity primitives remain
planned.

The first postflop bots should not each reinvent card parsing, hand evaluation,
pot arithmetic, or preflop ranges. Felt should provide a small C-facing bot kit
that is compiled into a bot. The bot still exports the same three functions from
`bot_api.h`; the kit does not change the bot ABI.

## Recommendation

1. Reuse Felt's already-vendored **OMPEval** for objective poker-hand ranking.
2. Write the small Felt-specific layer that turns a rank into useful concepts
   such as top pair, set, nut flush draw, and board texture. These concepts are
   not supplied by ordinary hand evaluators.
3. Keep made hand, draws, and equity as separate signals. A single ladder such
   as `air < weak draw < top pair < strong draw` is misleading because a hand
   can be top pair and a strong draw at the same time.
4. Give all initial postflop bots the same fixed 100 bb preflop chart. This
   isolates the postflop strategy in comparisons.
5. Do **not** solve preflop yet. A real preflop solution needs the value of all
   the postflop situations it reaches. With no postflop model, it would solve a
   toy game like the current shove-or-fold solver rather than normal hold'em.
6. Add range equity only after the cheap deterministic primitives and first
   heuristic bots work under the 200 µs decision cap.

The initial chart is named `baseline_100bb_v1`, not `solved` or `GTO`.
It is a controlled common starting policy, not a claim about optimal poker.

## What already exists

- `harness/third_party/ompeval` contains a pinned evaluator-only subset of
  OMPEval.
- `felt::evaluate7()` uses it for showdown ranking.
- `ExactEquityCalculator` calculates all-in adjusted results for the harness,
  but it knows both players' cards and therefore cannot be exposed directly to
  a bot.
- `scripts/finalize_match.py` has the statistics-side 169-hand bucket mapping.
- Individual bots currently derive ranks, suits, raise sizes, and history state
  directly from `FeltGameState`.

The first supported public layer between raw `bot_api.h` and a full strategy is
now in place; the more expensive strength and equity tiers remain to be added.

## Design rules

### API, not ABI

The kit is compiled into each bot and called internally by that bot. Changing
it does not require a `FELT_BOT_ABI_VERSION` bump; existing compiled bots keep
working, while source bots opt into changes when rebuilt.

### C interface, C++ implementation where useful

Bot authors should see a C11 API. Cheap helpers can be inline C. Evaluation and
equity can live in a static C++ library behind `extern "C"` wrappers, reusing
OMPEval. The build templates hide the C++ linkage so a normal bot can remain C.

### Pure and deterministic

Every function depends only on its arguments. Any randomized calculation takes
`decision_random` explicitly. No threads, clocks, OS randomness, file reads, or
opponent information are allowed. Immutable lookup tables and one-time
evaluator initialization are fine; adaptive state is not.

### Precise names instead of one vague strength number

The kit should distinguish:

- **made category:** what five-card hand exists now;
- **relative description:** top pair, underpair, set, and so on;
- **draws:** ways the hand can improve on later streets;
- **current showdown strength:** how the made hand compares with possible
  opposing hands on the board as it stands;
- **runout equity:** expected showdown share after unseen board cards are dealt.

Bots may combine these into a simple `weak / medium / strong` policy, but the
primitive layer should not destroy the useful distinctions.

## Primitive set

### 1. Cards, preflop classes, and deterministic randomness

These are header-only and effectively free:

```c
uint8_t  felt_rank(FeltCard card);                   /* 0 = 2, 12 = A */
uint8_t  felt_suit(FeltCard card);                   /* 0..3 */
uint16_t felt_preflop_class(FeltCard a, FeltCard b); /* canonical 0..168 */
const char *felt_preflop_label(uint16_t hand_class); /* e.g. "AKs" */
uint32_t felt_random_bounded(uint64_t decision_random,
                             uint64_t domain,
                             uint32_t bound);
```

The class mapping must be generated from one source and shared with the stats
pipeline. Test it across all 1,326 starting combinations so `76s` means the same
thing to a bot and to the match reports.

If a percentile helper is added, it must be combo-weighted: pairs represent six
combinations, suited hands four, and offsuit hands twelve. The 169 matrix cells
are not equally likely.

### 2. Betting context and legal action helpers

These eliminate repeated, error-prone history parsing:

```c
double    felt_pot_odds(const FeltGameState *state);
double    felt_spr(const FeltGameState *state);
FeltChips felt_big_blind(const FeltGameState *state);
double    felt_effective_stack_bb(const FeltGameState *state);

uint8_t felt_preflop_raise_count(const FeltGameState *state);
uint8_t felt_street_bet_count(const FeltGameState *state);
bool    felt_checked_to_me(const FeltGameState *state);
bool    felt_was_preflop_aggressor(const FeltGameState *state);
FeltPotClass felt_pot_class(const FeltGameState *state);

FeltAction felt_check_or_fold(const FeltGameState *state);
FeltAction felt_call_or_check(const FeltGameState *state);
FeltAction felt_raise_to_pot_fraction(const FeltGameState *state,
                                      double fraction);
FeltAction felt_raise_to_multiple(const FeltGameState *state,
                                  uint32_t multiple);
FeltAction felt_all_in(const FeltGameState *state);
```

The five action-producing helpers above are implemented. The betting-context
queries remain planned.

`felt_raise_to_pot_fraction` must return a total current-street contribution,
handle the short-all-in inverted bounds, and clamp only to legal amounts. Pot
classification must use exactly the same definitions as match statistics.

### 3. Made-hand description

OMPEval supplies the standard category and comparable rank. Felt adds context
relative to the board:

```c
typedef enum {
  FELT_MADE_HIGH_CARD,
  FELT_MADE_ONE_PAIR,
  FELT_MADE_TWO_PAIR,
  FELT_MADE_TRIPS,
  FELT_MADE_STRAIGHT,
  FELT_MADE_FLUSH,
  FELT_MADE_FULL_HOUSE,
  FELT_MADE_QUADS,
  FELT_MADE_STRAIGHT_FLUSH
} FeltMadeCategory;

typedef enum {
  FELT_PAIR_NONE,
  FELT_PAIR_UNDERPAIR,
  FELT_PAIR_BOTTOM,
  FELT_PAIR_MIDDLE,
  FELT_PAIR_TOP,
  FELT_PAIR_OVERPAIR
} FeltPairRelation;

typedef enum {
  FELT_TWO_PAIR_NONE,
  FELT_TWO_PAIR_BOARD_ONLY,
  FELT_TWO_PAIR_UNDER,
  FELT_TWO_PAIR_MIDDLE,
  FELT_TWO_PAIR_OVER,
  FELT_TWO_PAIR_BOTH_HOLE_CARDS
} FeltTwoPairKind;

typedef struct {
  uint16_t rank;               /* comparable OMPEval rank */
  FeltMadeCategory category;
  FeltPairRelation pair_relation;
  FeltTwoPairKind two_pair_kind; /* ordered heuristic strength band */
  uint8_t hole_kicker_rank;
  bool is_set;                 /* pocket pair + one board card */
  bool is_trips;               /* one hole card + paired board */
  bool plays_board;            /* hole cards do not improve the board */
  bool improves_board;
} FeltMadeHand;

FeltMadeHand felt_made_hand(const FeltCard hole[2],
                            const FeltCard *board,
                            uint8_t board_count);
```

The standard category is authoritative. Labels such as top pair, set, and the
two-pair strength band are additional facts, not replacements for it.
`two_pair_kind` orders the coarse heuristic cases as board-only, under, middle,
over, and both distinct hole cards making the two pairs. For under/middle/over,
the pair contributed by the hole cards is compared with board ranks outside the
best two pairs: all above is under, ranks on both sides is middle, and none above
is over. It follows the best five cards, so a lower counterfeited pair does not
count as hole-card participation.

The inline `felt_is_top_pair_or_better()` convenience predicate includes top
pair, overpairs, over two pair, two pair made with both distinct hole cards, and
every category from trips upward. Under/middle two pair deliberately sit below
that threshold, while board-only two pair does not count as player-made value.
High card is not automatically called "air": once draw classification exists,
air means a high-card hand without a relevant draw.

### 4. Draws and immediate improving cards

Draws should be a bitmask because several can coexist:

```c
enum FeltDrawFlag {
  FELT_DRAW_NONE              = 0,
  FELT_DRAW_OVERCARDS         = 1 << 0,
  FELT_DRAW_GUTSHOT           = 1 << 1,
  FELT_DRAW_OPEN_ENDED        = 1 << 2,
  FELT_DRAW_DOUBLE_GUTSHOT    = 1 << 3,
  FELT_DRAW_FLUSH             = 1 << 4
};

typedef struct {
  bool valid;
  uint32_t flags;
  uint8_t improving_next_cards;
  uint8_t straight_next_cards;
  uint8_t flush_next_cards;
  bool nut_flush_draw;
} FeltDraws;

FeltDraws felt_draws(const FeltCard hole[2],
                     const FeltCard *board,
                     uint8_t board_count);
```

Count unique unseen next cards rather than adding memorized "four and eight
out" rules; this avoids double-counting combo draws. Call them *improving* outs,
not clean outs. Whether an out actually wins depends on the opponent's range.

The initial implementation covers live one-card overcard, straight, and flush
draws. It deliberately does not label backdoor-only possibilities. On the
river the classification remains valid but returns no flags or outs, so a
missed draw is air to a strategy.

A convenience policy may later map draws to `NONE / WEAK / STRONG`, for
example treating an open-ended straight draw, flush draw, double gutshot, or
combined pair-plus-draw as strong. That mapping belongs in a heuristic profile,
not in the ground-truth feature extractor.

### 5. Board texture

Avoid a single unexplained `wetness` score. Expose facts that a bot can combine:

```c
typedef struct {
  uint8_t high_rank;
  uint8_t broadway_count;
  uint8_t distinct_rank_count;
  uint8_t max_suit_count;
  uint8_t max_cards_in_five_rank_window;
  uint8_t pair_count;
  bool trips_on_board;
  bool quads_on_board;
  bool straight_on_board;
  bool flush_on_board;
} FeltBoardTexture;

FeltBoardTexture felt_board_texture(const FeltCard *board,
                                    uint8_t board_count);
```

This supports simple rules such as "c-bet small on unpaired rainbow boards" or
"do not stack off one pair on a four-flush board" without pretending there is
one universally correct texture ordering.

### 6. Current strength against all possible hands

This answers "how strong is my hand right now?" with the visible board frozen.
Enumerate every legal opposing two-card combination, respecting blockers:

```c
typedef struct {
  uint32_t ahead;
  uint32_t tied;
  uint32_t behind;
  double showdown_share; /* (ahead + tied / 2) / total */
} FeltCurrentStrength;

FeltCurrentStrength felt_current_strength_vs_random(
    const FeltCard hole[2], const FeltCard *board, uint8_t board_count);
```

This is exact and cheap postflop: there are only about one thousand possible
opposing combinations. It does **not** include future turn or river cards. On a
flop, a flush draw can therefore have weak current strength but high runout
equity; that difference is intentional and useful.

Later, accept a precompiled opponent range as the comparison set. Do not parse
a range string from scratch on every timed decision.

### 7. Opponent ranges

Equity against "all hands" is a useful baseline, but action-aware bots need a
range representation too:

```c
typedef struct FeltRange FeltRange; /* 1,326 combo weights, 0..1000 */

bool felt_range_parse(FeltRange *out, const char *text);
double felt_range_combo_count(const FeltRange *range,
                              const FeltCard *known,
                              uint8_t known_count);
```

Reuse OMPEval's EquiLab-style notation for inputs such as `QQ+,AKs,AcQc` and
compile named ranges before timed decisions. Filtering for visible blockers
must not mutate the original range.

For the first equity bot, infer the opponent's preflop reaching range from
`baseline_100bb_v1` and the observed preflop line. Initially leave that range
unchanged postflop. Updating it after bets and calls requires explicit modeling
assumptions and should be a later strategy feature, not hidden inside the kit.

### 8. Runout equity versus random and versus a range

This answers a different question: "what fraction of the final pot should this
hand win after all remaining board cards?"

```c
typedef struct {
  double win;
  double tie;
  double equity;      /* win + tie / 2 */
  uint32_t samples;
  bool exact;
} FeltEquity;

FeltEquity felt_equity_vs_random(const FeltCard hole[2],
                                 const FeltCard *board,
                                 uint8_t board_count,
                                 uint32_t sample_budget,
                                 uint64_t decision_random);

FeltEquity felt_equity_vs_range(const FeltCard hole[2],
                                const FeltCard *board,
                                uint8_t board_count,
                                const FeltRange *opponent,
                                uint32_t sample_budget,
                                uint64_t decision_random);
```

Use OMPEval's hand evaluator and range notation, but implement a small
single-threaded deterministic enumeration/Monte Carlo loop for bots. OMPEval's
stock equity calculator automatically uses worker threads and does not fit
Felt's single-threaded timing contract directly.

Prefer exact enumeration where the state space is small (especially the river,
and likely the turn after benchmarking) and deterministic Monte Carlo on the
flop. Return sample count and whether the answer is exact so strategies know the
quality of the number.

## Preflop chart plan

### Why not solve it now

Preflop actions cannot be valued independently from postflop. A 2.5 bb open is
good or bad partly because of how both ranges play thousands of flop, turn, and
river situations. Feeding a solver "check down after the flop" or "always jam"
would produce a precise solution to an artificial game and bake those mistakes
into every future bot.

Generic public charts also vary by heads-up versus six-max, rake, stack depth,
open size, allowed bet sizes, and whether limping is included. A familiar chart
with mismatched assumptions is not ground truth.

### Initial chart

Use one transparent **heads-up, no-rake, 100 bb** reference chart unchanged in
every first-generation postflop bot. Felt currently starts at 200 bb, so this is
an acknowledged temporary baseline rather than an exact match for the game.

Version 1 is committed directly as readable A-to-2 matrices and named hand
ranges in `harness/src/preflop_chart.cpp`; bots do no file I/O. If the chart
becomes cumbersome to edit, a later generator can move the source ranges into
a data file without changing the public API.

Minimum spot set:

| Spot | Available policy |
|---|---|
| Button first in | fold / limp / open to a fixed BB size |
| Big blind versus limp | check / raise |
| Big blind with less than 6 bb left to call | fold / call / re-raise |
| Button with less than 6 bb left to call | fold / call / re-raise |
| Either player with 6 to under 16 bb left to call | fold / call / re-raise |
| Either player with 16 to under 22 bb left to call | fold / call / re-raise |
| Either player with 22 to under 45 bb left to call | fold / call / jam |
| Either player with at least 45 bb left to call | fold / jam, calling only when the opponent is already all-in |

The chart uses history to distinguish unopened and limped pots, but once a
raise exists it selects the response from the additional amount the bot must
call: below 6, 6 to under 16, 16 to under 22, 22 to under 45, or at least 45
bb. Thus a 40 bb open costs the big blind 39 bb and uses the five-bet range,
while a raise to 50 bb after the bot has already contributed 30 bb costs 20 bb
and uses the four-bet range. Openers are fixed at 2.5 bb and 4 bb; every re-raise is a multiple of
the raise in front of it -- 3.5x for a three-bet, 3x for a four-bet, 2x for a
five-bet or beyond -- and clamps to the legal range, so a multiple past the
stack becomes an all-in. Impossible or unsupported lines use a safe fold/check fallback and are
covered by tests.

Version 1 uses pure actions and needs no randomness. If mixed cells are added
later, they should use `decision_random`, the hand class, and a chart-specific
domain tag so decisions remain reproducible and duplicate-symmetric.

### How `baseline_100bb_v1` is populated

The built-in range transcribes the supplied 100 bb reference screenshots into
pure 169-class actions. Deeper 4-bet and shove responses are conservative,
documented heuristics. See [charts/README.md](charts/README.md) for ranges,
combo counts, fixed sizes, and the approximations made during transcription.

After the first postflop strategy exists, replace or compare this baseline with
an offline solve whose game exactly matches Felt: 200 bb, heads-up, no rake,
the same raise menu, and an explicit postflop abstraction. The generated chart
should record solver version, parameters, exploitability/convergence measure,
and source commit. Solver code should remain an offline tool, not a runtime bot
dependency.

## Open-source choices

| Project | Use in Felt | Decision |
|---|---|---|
| [OMPEval](https://github.com/zekyll/OMPEval) (ISC) | Hand ranking; range parser; basis of our deterministic equity loop | **Use.** Already pinned and used by the harness, so there is one ranking authority. |
| [PokerHandEvaluator](https://github.com/HenryRLee/PokerHandEvaluator) (Apache-2.0) | Fast C/C++ five-to-seven-card evaluator | Keep as fallback only. It has a clean C API, but using two evaluators creates avoidable disagreement risk. |
| [PokerStove](https://github.com/andrewprock/pokerstove) | Evaluation/enumeration framework | Do not add. It is broader and established, but brings Boost and duplicates functionality Felt already has. |
| [TexasSolver](https://github.com/bupticybee/TexasSolver) (AGPL-3.0) | Offline postflop solving experiments | Do not vendor or link. It is postflop-focused and its license/weight are unsuitable for the bot kit; it may be used separately for research if its terms are followed. |

No existing library should be trusted to define Felt-specific labels such as
top pair, set versus trips, or "strong draw." Those are policy and board-context
concepts, so Felt owns and tests their definitions.

The current preflop chart comes from user-supplied reference images. It is
documented as a baseline rather than redistributed or presented as solved data.

## Packaging

Proposed public surface:

```text
harness/include/felt/bot_kit.h       C declarations and inline helpers
harness/src/bot_kit.cpp              made-hand and texture implementation
harness/src/preflop_chart.cpp        built-in chart and direct action helper
harness/third_party/ompeval/         pinned upstream evaluator/range subset
bots/charts/README.md                chart assumptions, ranges, and sizes
tools/gen_bot_tables                 future deterministic equity-table generator
```

`add_felt_bot_with_kit` links `libfelt_botkit.a` for bots that opt into the kit.
Existing tiny reference bots continue to use `add_felt_bot` and pay no extra
binary or initialization cost. Update the C and C++ standalone templates once
the first postflop bot exercises the API end to end.

Call `felt_bot_kit_warmup()` once from `felt_bot_name()`, which the harness calls
outside the decision timer. Every actual query remains charged to the normal
bot CPU cap.

## Validation

### Correctness tests

- Golden examples for every made category and pair relation.
- Paired-board, counterfeit, wheel, board-straight, and board-flush edge cases.
- Every straight and flush draw type, including overlapping combo draws.
- Exhaustive next-card checks for `improving_next_cards`.
- Suit-permutation invariance and hole-card-order invariance.
- OMPEval agreement with the harness evaluator on randomized five-, six-, and
  seven-card inputs.
- All 1,326 preflop combos agree with the statistics bucket label.
- Every chart spot has exhaustive 1,326-combo count tests and every emitted
  direct action is legal.
- If mixed chart actions are added, fixed `decision_random` values reproduce
  identical choices.

### Timing tests on macOS

Benchmark each primitive separately in release mode. Record median and p99, not
just average. The acceptance target is:

- context, chart, made-hand, draw, and texture helpers are comfortably below
  25 µs together;
- exact current-strength enumeration uses an explicitly larger search profile
  unless measurement proves it fits below 200 µs;
- equity helpers obey an explicit sample budget and leave margin for strategy
  code;
- no helper creates threads.

If equity cannot meet 200 µs reliably, it stays opt-in and equity-based bots run
with a declared larger cap. The basic heuristic bots must not need it.

## First bots built on the kit

All use `baseline_100bb_v1` preflop so their postflop behavior is the variable:

1. **`slp-fold`** — value-bets top pair/overpair, over two pair,
   both-hole-card two pair, and trips or better; checks/calls smaller pairs,
   under/middle two pair, and live draws; and gives up with air.
2. **`slp-bluff`** — the same value policy but attacks every air hand.
3. **`slp-balance`** — the same policy with a deterministic 50% air bluff
   frequency when checked to; it folds air to aggression and takes a passive
   line with its value range on one-third of eligible decisions. It calls two
   thirds of weak pairs and draws against an opening bet. It treats board-only
   two pair as air, under/middle two pair like smaller pairs, over two pair like
   an overpair, and both-hole-card two pair as the strongest two-pair band.
   Facing aggression it only reraises with trips or better.
4. **`slp-exploit-fold`** — always attacks air when checked to and
   folds to aggression without an overpair or better.
5. **`slp-exploit-solved`** — open-min-raises every hand into
   `solved-all-in`, uses the target's solved all-in response range, returns to
   the shared chart against ordinary reraises, and reuses the fold exploit
   postflop.
6. **texture-aware bot** — changes c-bet frequency and size using position,
   initiative, and board facts.
7. **equity-threshold bot** — compares range/random equity with pot odds and a
   safety margin; this is the first consumer of Tier 7.

Start with deterministic pure strategies. Add mixed frequencies only where a
specific experiment needs them, so failures remain easy to understand.

## Implementation order

1. ~~Freeze made-hand, board-texture, and chart definitions in tests.~~
2. ~~Add the shared 169-class mapping and `baseline_100bb_v1` lookup/action
   helper.~~
3. ~~Add OMPEval-backed made-hand ranking plus Felt's pair/set/trips labels.~~
4. ~~Add factual board-texture features.~~
5. Add context, history, and general legal-action helpers. *(The first legal
   action subset is complete; context/history queries remain.)*
6. ~~Add live draw classification and unique immediate improving-card counts.~~
7. Add exact current strength versus all legal random hands.
8. Build and benchmark successive heuristic bots. *(The four `slp`
   air-policy variants and the solved-all-in exploit are built; texture-aware
   remains.)*
9. Add precompiled ranges and derive reaching ranges from the shared chart.
10. Add deterministic runout equity only when the first equity bot is ready.
11. Revisit a real preflop solve after postflop behavior and action abstraction
    are concrete enough to value reached states.

This order gets useful postflop bots early and avoids making the hardest,
slowest primitive—range equity—a dependency of every strategy.

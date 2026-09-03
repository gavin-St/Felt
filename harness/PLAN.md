# Harness Implementation Plan

Roadmap for building `run_match` per [SPEC.md](../SPEC.md). No code exists yet;
this file is the sequencing document. Each milestone lists its goal, concrete
steps, exit criteria, and the decisions that must be settled before or during it.

**Guiding order:** correctness before speed, and get an end-to-end match running
as early as possible (M4) so every later milestone has something to run against.

Dependency chain:

```
M0 ─┬─ M1 ── M6
    ├─ M2 ─┬─ M4 ─┬─ M5 ── M9
    └─ M3 ─┘      ├─ M7
                  └─ M8 ── M9
                          all ── M10
```

---

## M0 — Foundations

**Goal:** a repo that builds, tests, and has the core value types settled.

Steps:

1. CMake project, C++17, warnings-as-errors, Debug/Release/ASan+UBSan presets.
2. Test framework (Catch2 or doctest, vendored or FetchContent).
3. Core types: `Card` (uint8 0–51, rank = `c >> 2` or `c / 4` — pick one and
   document it), `Deck`, `Street`, `Chips` (int64 to avoid overflow in stats).
4. Deterministic RNG: pick one PRNG (xoshiro256++ or PCG64) and use it
   everywhere. Seed derivation helper: `hash(match_seed, hand_index)` with a
   named, stable hash (SplitMix64) so seeds are reproducible across builds.
5. Card string parsing/formatting (`"Ah"` ↔ 51) for tests and logs.

**Done when:** `cmake --build && ctest` is green with round-trip card tests and
a PRNG determinism test (same seed → same sequence, pinned golden values).

**Decide here:** card encoding convention. Everything downstream depends on it
and changing it later touches the evaluator, dealer, logs, and stats.

---

## M1 — Hand evaluator

**Goal:** correct, fast 7-card evaluation.

Steps:

1. Choose OMPEval vs 2+2 lookup table (see decision note below); vendor into
   `third_party/`.
2. Wrap behind a thin `evaluate7(Card[7]) -> uint32` interface so the backend
   can be swapped.
3. Correctness tests: known hand rankings, all category boundaries, ties/chops,
   wheel straight (A-2-3-4-5), steel wheel, quads vs full house, kicker cases.
4. Cross-check: brute-force reference evaluator over a large random sample —
   both must agree on the *ordering* of every pair.
5. Benchmark: evaluations/sec in Release.

**Done when:** cross-check passes on ≥10M random 7-card hands and the benchmark
is recorded in the README.

**Decision — evaluator backend:** 2+2 is a ~130 MB table (fits the 1 GB limit but
costs load time and cache misses); OMPEval is smaller and header-ish, no big
table load. OMPEval is the better default; revisit only if M10 shows the
evaluator is the bottleneck.

---

## M2 — Game engine (betting + showdown)

**Goal:** rules-correct heads-up NLHE for a single hand, bots stubbed out.

Steps:

1. `HandState`: stacks, pot, current bets, street, whose turn, betting-round
   termination.
2. Preflop HU button rule: BTN/SB acts first preflop, BB acts first postflop.
   This is the single most commonly-botched HU detail — test it first.
3. Legal action computation: `to_call`, `min_raise_to`, `max_raise_to`, and
   whether check/fold/bet/raise are available.
4. Raise-sizing rules: min-raise increment = size of the last raise; an all-in
   that is *less* than a full raise increment does **not** reopen action for a
   player who has already acted.
5. Uncalled bet returned; pot awarded; chopped pots split (odd chip → rule must
   be fixed and documented, e.g. to the BB/out-of-position player).
6. Showdown via M1 evaluator.
7. Action history recording (this hand only) in the shape `GameState` exposes.

**Done when:** a hand-scripted test suite covers: limp/check-through, 3-bet/4-bet
lines, short all-in not reopening action, all-in call for less, fold to a bet at
each street, chopped board, and a walk (BB wins blinds when SB folds).

**Note:** with `--stack` applied equally to both seats and stacks reset every
hand, effective stacks are always equal, so **true side pots cannot occur**. The
engine should still assert this invariant rather than assume it silently, in case
the reset rule is ever relaxed.

---

## M3 — Bot ABI and loading

**Goal:** load two `.so` files and call into them safely.

Steps:

1. Public headers in `harness/include/`: `bot.h`, `game_state.h`, `action.h`.
   These are the frozen ABI surface — treat changes as breaking.
2. `GameState` layout: POD/const-ref, no owning containers exposed across the
   boundary if avoidable (prefer spans/raw arrays + counts).
3. ABI version constant exported by the harness and checked against the bot.
4. `dlopen` with `RTLD_NOW | RTLD_LOCAL` so two bots cannot collide on symbol
   names; resolve `create_bot`; guard the case where both arguments are the
   *same* path (dlopen returns one handle — must still yield two independent
   instances).
5. Bot lifetime: construct via `create_bot()` and destroy every hand, per spec.
6. Action validation layer: illegal or out-of-range action → default action
   (check if legal, else fold) + logged violation. Includes amount clamping,
   NaN/negative amounts, and actions of the wrong type for the state.
7. Reference bots in `bots/`: `always_fold`, `always_call`, `random` (using
   `rng_seed` only).

**Done when:** `run_match` can load two reference bots and play one hand
end-to-end, and a deliberately-illegal bot produces the documented default
action plus a violation record.

**Decide here:** per-hand `create_bot()` is cheap for simple bots but punishing
for one that builds lookup tables in its constructor. Either (a) accept it and
tell bot authors to use function-local statics / lazily-initialized globals for
immutable tables, or (b) add an explicit `init()` hook outside the timed path.
Option (a) keeps the spec's "no lifecycle hooks" rule; document it either way.

---

## M4 — Match runner (vertical slice)

**Goal:** a full match runs end to end and is bit-for-bit reproducible.

Steps:

1. CLI parsing for the full flag set in the spec, with validation and `--help`.
2. Dealer: per-hand seed = `hash(match_seed, hand_index)`; deal from a fresh
   shuffled deck each hand so the deal depends only on that seed.
3. Duplicate pairing: each deal played twice with seats swapped. Fix and document
   whether pair *k* is hands `2k`/`2k+1` (adjacent) — adjacent is simpler and
   makes `--hands` odd/even handling explicit (round down to a whole pair, or
   error).
4. `--no-duplicate` path.
5. Chip accounting across the match; per-seat and per-bot totals.
6. Determinism harness: same seed + same bots → byte-identical output.

**Done when:** `run_match always_fold.so always_call.so --hands 1000` completes,
the duplicate-pair chip totals reconcile exactly, and two runs with the same seed
produce identical files.

**Sanity check available here:** always_fold vs always_call has a known analytic
expectation (folder loses the blinds every hand) — a cheap end-to-end oracle.

---

## M5 — Timing and resource enforcement

**Goal:** enforce the timing contract without letting a bad bot hang the match.

Steps:

1. Worker thread per decision path: bot runs on a dedicated thread; harness waits
   on a `steady_clock` deadline and discards late results.
2. Soft cap (default 2 ms) with a per-match time bank (default 10 s); bank
   accounting and depletion → default action + soft violation.
3. Hard ceiling (default 200 ms) → default action + hard violation.
4. Forfeit on 3 hard violations, or on any exception/crash escaping the bot.
5. Exception safety: wrap `act()` in a catch-all; a bot that throws forfeits
   rather than corrupting engine state.
6. Resource limits: 1 core (affinity or `rlimit`), 1 GB (`RLIMIT_AS`), no network
   — decide the mechanism (see below).
7. Timing instrumentation: per-decision durations captured for M7 stats.

**Done when:** adversarial test bots — sleep-forever, sleep-just-over-cap,
throw, allocate-unbounded — each produce exactly the documented outcome, and the
match still terminates.

**Decide here — the hung-bot problem.** A thread stuck in an infinite loop cannot
be safely killed in-process. Options: (a) detach and leak the thread, accept the
memory, forfeit the match, exit the process; (b) run each bot in a *child
process* behind a pipe, which makes kill/limits/network-isolation trivial but
adds IPC cost per decision — at a 2 ms cap that overhead is significant;
(c) leak-and-forfeit for hangs, in-process for everything else. **(c) is the
recommended default** given the 2 ms cap, with (b) noted as the fallback if real
sandboxing is ever required against untrusted bots. Note plainly in the README
that in-process bots are *not* a security boundary.

**Also decide:** "no network" — enforced (seccomp/namespace) or contractual?
Enforced requires the child-process model. Contractual is fine for bots you wrote
yourself; say so explicitly rather than implying a sandbox that doesn't exist.

---

## M6 — All-in equity adjustment

**Goal:** when both players are all-in before the river, award pot × exact equity.

Steps:

1. Detect the all-in-before-river state and the street it occurred on.
2. Exact enumeration for turn (44 boards) and flop (990 boards) — cheap, do it
   directly.
3. Preflop all-in: C(48,5) = 1,712,304 boards per matchup. Too slow to do naively
   at every occurrence. Mitigations, in order of preference:
   - memoize on the canonical (hole_a, hole_b) key — duplicate poker guarantees
     each matchup recurs, and a bot with a shoving strategy will repeat them;
   - suit-isomorphism canonicalization to collapse the key space;
   - optional precomputed preflop table if profiling still shows it dominating.
4. Runout is still dealt and logged even when the result is equity-adjusted.
5. Raw and adjusted results both tracked, per spec.
6. `--no-equity-adjust` path.

**Done when:** equity results match known values for canonical matchups (AA vs KK
preflop ≈ 82.36%, AKs vs QQ ≈ 46.0%, coin-flip cases), and a 40k-hand match with
a shove-happy bot stays inside the wall-time target.

**Watch:** an all-in-preflop-every-hand bot is the pathological case for this
milestone. Build it as a test bot and profile against it.

---

## M7 — Statistics

**Goal:** the three output files, correct and reconciling.

Steps:

1. Accumulators sized up front: 169 starting-hand buckets × 2 positions = 338
   rows; 1,326 exact combos.
2. Combo → 169-bucket mapping (pair / suited / offsuit) with tests.
3. Per-hand fields: dealt, VPIP, PFR, win %, showdown %.
4. Per-street: fold/check/call/bet/raise frequencies, c-bet %, WTSD, W$SD.
   **Pin down the precise definition of each** before implementing — these are
   the stats people argue about. Write the definitions into the README.
5. Per-position bb/100 (BTN vs BB), headline totals, chips, hands won/lost/chopped.
6. Timing stats: mean / p99 / max decision time, bank used, soft and hard
   violation counts.
7. Writers: `summary.json`, `hand_stats.csv`, `combo_stats.csv`.

**Done when:** cross-footing checks pass (combo rows sum to bucket rows sum to
headline totals; VPIP ≤ 100%; hands won + lost + chopped = hands played), and a
scripted-line test match produces hand-verified numbers.

---

## M8 — Logging and replay

**Goal:** every hand reconstructible from the log.

Steps:

1. Per-hand JSONL: `hand_index`, seed, both holes, board, full action sequence
   with per-decision timing, raw and adjusted result.
2. Buffered writer — at ~8 MB/match and 40k hands, unbuffered per-line I/O will
   show up in the wall-time budget.
3. `replay <hand_index>` subcommand reproducing a hand from its seed.
4. Round-trip test: replay reproduces the logged actions and result exactly.

**Done when:** a random sample of hands from a completed match replays to
identical action sequences and results.

**Decide here:** should `replay` reconstruct from the *seed* (regenerating bot
decisions) or from the *logged actions*? Seed-based is the stronger guarantee and
doubles as a statelessness check; log-based is easier but proves less.
Recommend seed-based.

---

## M9 — Statelessness verification

**Goal:** catch bots that smuggle state across hands.

Steps:

1. `--verify-stateless <fraction>` sampling (default 0.01).
2. Replay sampled hands against a *fresh* bot instance with identical
   `GameState` inputs; compare action sequences.
3. Mismatch → match invalidated, with a report naming the diverging decision.
4. Test bots that deliberately cheat: a hand counter in a global, a static that
   remembers the opponent, a bot seeded from wall-clock time.

**Done when:** each cheating test bot is caught, and honest reference bots never
trigger a false positive across a full 40k-hand match.

**Caveat to document:** this catches *naive* cheating. A bot that only diverges
on the 99th hand of a pattern, or that keys off allocator addresses, can slip
through a 1% sample. State the guarantee honestly in the README.

---

## M10 — Performance, hardening, release

**Goal:** hit the 7–15 min wall-time target and make the binary pleasant to use.

Steps:

1. Profile a full 40k-hand match; expected hotspots are the evaluator, equity
   enumeration, JSONL writing, and per-hand bot construction.
2. Optimize against measurements only — no speculative tuning before this point.
3. Run the full suite under ASan/UBSan and under Valgrind for the dlopen paths.
4. Long-run soak: several full matches, checking for leaks and drift.
5. Docs: README with build instructions, the bot-author guide (ABI, statelessness
   contract, timing contract, what the harness does *not* sandbox), and the stat
   definitions from M7.
6. Resolve the two open items flagged at the bottom of SPEC.md (per-street stat
   list, Elo `k`).

**Done when:** default-config match completes in target time on the reference
machine, sanitizers are clean, and a new bot can be written from the docs alone.

---

## Cross-cutting decisions to settle early

| Decision | Why it's urgent | Recommendation |
|---|---|---|
| Card encoding (`rank = c >> 2` vs `c / 4`) | Touches every component | Pick in M0, document in `game_state.h` |
| Evaluator backend | Determines `third_party/` and load-time cost | OMPEval; revisit at M10 |
| In-process vs child-process bots | Reshapes M5 entirely; hard to retrofit | In-process, leak-and-forfeit on hang |
| `create_bot()` per hand vs init hook | Affects the bot-author contract | Per hand + document the statics idiom |
| Odd-chip rule on chopped pots | Silent 1-chip bias over 40k hands | Fix and document in M2 |
| `--hands` odd with duplicate on | Breaks pairing | Error out rather than silently truncate |

## Deliberately out of scope

Elo (separate tool, `elo/`), multiway play, tournament/ICM, variable stack depths,
bot-vs-human play, and any web/visualization layer.

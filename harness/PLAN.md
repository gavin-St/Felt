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
3. Core types: `Card` (uint8 0–51, **rank-major**: `rank = c >> 2`,
   `suit = c & 3`), `Deck`, `Street`, `Chips` (int64 to avoid overflow in stats).
4. Deterministic RNG: pick one PRNG (xoshiro256++ or PCG64) and use it
   everywhere. Seed derivation helper: `hash(match_seed, hand_index)` with a
   named, stable hash (SplitMix64) so seeds are reproducible across builds.
5. Card string parsing/formatting (`"Ah"` ↔ 51) for tests and logs.

**Done when:** `cmake --build && ctest` is green with round-trip card tests and
a PRNG determinism test (same seed → same sequence, pinned golden values).

**Settled — card encoding: rank-major, `rank = c >> 2`, `suit = c & 3`.** Both
are single instructions, and card values then order by rank, so sorting a hand
sorts by rank for free. It is also expected to match OMPEval's own convention,
avoiding a conversion on the hot path — **confirm against the vendored header at
M1**; if it differs, the fix is local to `evaluate7()`.

---

## M1 — Hand evaluator

**Goal:** correct, fast 7-card evaluation.

Steps:

1. Vendor OMPEval into `third_party/` (settled; rationale below).
2. Wrap behind a thin `evaluate7(Card[7]) -> uint32` interface so the backend
   can be swapped.
3. Correctness tests: known hand rankings, all category boundaries, ties/chops,
   wheel straight (A-2-3-4-5), steel wheel, quads vs full house, kicker cases.
4. Cross-check: brute-force reference evaluator over a large random sample —
   both must agree on the *ordering* of every pair.
5. Benchmark: evaluations/sec in Release.

**Done when:** cross-check passes on ≥10M random 7-card hands and the benchmark
is recorded in the README.

**Settled — evaluator backend: OMPEval.** The alternative, the 2+2 evaluator, is
a ~130 MB state-machine table where evaluation is seven array indexings and no
branches. Lower instruction count, but random access across 130 MB means nearly
every lookup is a cache and TLB miss, which in practice gives back most of that
advantage — and the table has to be generated or shipped. OMPEval is a few MB,
vendors cleanly, and is friendlier to cache. Low-stakes either way: step 2 keeps
it behind `evaluate7()`, so swapping backends is a one-file change if M10 says
the evaluator is the bottleneck.

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
5. Uncalled bet returned; pot awarded; chopped pots split with the **odd chip to
   the BB** (settled). Note this is a fixed seat bias, which duplicate poker
   cancels exactly; it only shows up under `--no-duplicate`.
6. Showdown via M1 evaluator.
7. Action history recording (this hand only) in the shape `GameState` exposes.

**Done when:** a hand-scripted test suite covers: limp/check-through, 3-bet/4-bet
lines, short all-in not reopening action, all-in call for less, fold to a bet at
each street, chopped board, and a walk (BB wins blinds when SB folds).

**Settled:** stacks are equal for both seats and reset every hand, so effective
stacks are always equal and **true side pots cannot occur**. The engine asserts
this invariant rather than assuming it silently. The only all-in subtleties that
remain are the uncalled-bet return and the short all-in that does not reopen
action (step 4).

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
5. Bot lifetime: construct via `create_bot()` once per match (see below).
6. Action validation layer: illegal or out-of-range action → default action
   (check if legal, else fold) + logged violation. Includes amount clamping,
   NaN/negative amounts, and actions of the wrong type for the state.
7. Reference bots in `bots/`: `always_fold`, `always_call`, `random` (using
   `rng_seed` only).

**Done when:** `run_match` can load two reference bots and play one hand
end-to-end, and a deliberately-illegal bot produces the documented default
action plus a violation record.

**Settled: `create_bot()` once per match, not once per hand.** Per-hand
construction was in the spec as a statelessness guard, but it does not act as
one: destroying and recreating the object leaves globals, function-local statics,
and any mmap'd file untouched, so a bot that wants to carry state just writes
`static int hand_count` and never notices. It pays a real cost every hand and
buys nothing. Statelessness is instead a documented contract, verified by the
sampled replay check in M9.

**Optional hard enforcement: `--fork-per-hand`.** Building on M5's child-process
model, the child becomes a zygote that dlopens and performs one-time init; each
hand forks a worker from it that plays that hand and exits. A fresh address space
per hand resets globals too, so statelessness is enforced by the OS rather than
by convention. Copy-on-write means the bot's immutable lookup tables are built
once in the zygote and inherited free. Cost ~100–200 µs/hand (~2–4% of wall
time) — measure at M10 before deciding whether ranked matches feeding Elo should
default to it.

This also dissolves the bot-init problem: expensive table construction happens
once in the zygote, before any hand is dealt and outside the timed path. No
lifecycle hook is required, so the spec's "no lifecycle hooks" rule stands.

---

## M4 — Match runner (vertical slice)

**Goal:** a full match runs end to end and is bit-for-bit reproducible.

Steps:

1. CLI parsing for the full flag set in the spec, with validation and `--help`.
2. Dealer: per-hand seed = `hash(match_seed, hand_index)`; deal from a fresh
   shuffled deck each hand so the deal depends only on that seed.
   Note: blinds 50/100 with 20,000 stacks reset every hand is exactly the ACPC
   heads-up no-limit format, known as *Doyle's Game*. Keeping these numbers means
   results are directly comparable to published computer-poker work, so they are
   fixed defaults rather than arbitrary ones. Blind *values* are otherwise
   scale-free — only chip granularity changes — and no ante is used.
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

## M5 — Process isolation, timing, and resource enforcement

**Goal:** enforce the timing contract and survive any bot failure, including a crash.

**Settled: bots run in child processes, not in-process threads.** The deciding
argument is the spec's own "exception/crash → forfeit" rule: the harness must
survive a segfault in bot code, record the forfeit, and still write results. A
SIGSEGV handler plus longjmp cannot do that reliably and leaves the allocator in
an undefined state. Process isolation is therefore required, not merely nicer.
Hang handling then falls out for free.

Steps:

1. Fork one child per bot at match start; child applies limits, then dlopens.
2. Transport: shared memory (`GameState` in, `Action` out — both POD, so memcpy)
   plus a futex or eventfd pair. Parent signals, then waits with a
   `CLOCK_MONOTONIC` deadline. Cost ~5–20 µs per decision: ~1% of the 2 ms cap
   and ~2–3% of match wall time.
3. Soft cap (default 2 ms) with a per-match time bank (default 10 s); bank
   accounting and depletion → default action + soft violation.
4. Hard ceiling (default 200 ms) → default action + hard violation, then SIGKILL
   the child and fork a replacement. Fork+dlopen costs ~1–10 ms, paid only on
   violation, and violations are capped at 3 before forfeit anyway.
5. Crash or unexpected child exit → parent observes it via SIGCHLD/eventfd close
   → forfeit, flush outputs, exit cleanly. This is the case in-process cannot do.
6. Forfeit on `--max-hard-violations` hard violations (default 3), or on any
   crash or exception escaping the bot.
7. Limits applied in the child between fork and dlopen:
   - `RLIMIT_AS` → 1 GB;
   - `sched_setaffinity` → 1 core (parent and children share it safely, since
     exactly one is runnable at a time);
   - seccomp filter blocking socket syscalls → "no network" becomes *enforced*
     rather than contractual.
8. Timing instrumentation: enforce on the parent-side measurement, since that is
   what the deadline governs, but have the child report its own span too. A gap
   between the two indicates IPC or scheduling cost, not bot logic. Both are
   recorded for M7.

**Done when:** adversarial test bots — hang-forever, sleep-just-over-cap, throw,
segfault, allocate-unbounded, open-a-socket — each produce exactly the documented
outcome, and the match still terminates with complete output files in every case.

**Rejected: in-process worker threads.** Cheaper per decision, but a thread stuck
in an infinite loop cannot be safely killed; `pthread_cancel` corrupts C++
destructors and can leave the allocator locked. The pathological case is a bot
that hangs inside `malloc` while holding the allocator lock, which deadlocks the
harness permanently. Combined with the crash requirement above, this is not
recoverable.

---

## M6 — All-in equity adjustment

**Goal:** when both players are all-in before the river, award pot × exact equity.

Steps:

1. Detect the all-in-before-river state and the street on which it occurred.
2. Flop (990 boards) and turn (44 boards): enumerate live. Cheap, no table.
3. Preflop (C(48,5) = 1,712,304 boards per matchup): precomputed table, generated
   offline. See sizing below.
4. Runout is still dealt and logged even when the result is equity-adjusted.
5. Raw and adjusted results both tracked, per spec.
6. `--no-equity-adjust` path.

**Settled: store exact win/tie counts, not floating-point equity.** Each entry is
two `uint32` (`wins_a`, `ties`; `wins_b` is implied by the board total). Pot
splitting is then exact integer arithmetic —
`pot * (2*wins_a + ties) / (2 * total_boards)` — with one documented rounding
rule, and results are bit-identical across compilers and architectures. Floats
would put the harness's core reproducibility guarantee at the mercy of FP
contraction and libm differences.

**Settled: combo-level, not hand-class.** Published 169×169 preflop charts are
*not* exact: AKs vs QQ differs depending on whether the ace-king shares a suit
with one of the queens. Exactness requires the full 1,326 × 1,326 combo table.

Sizing — storage is a non-issue; generation is the real cost:

| Representation | Size |
|---|---|
| 1,326 × 1,326 exact counts, flat | 14.1 MB |
| same, exploiting A/B symmetry | 7.0 MB |
| ~47,008 suit-isomorphic matchups + canonicalization on lookup | 376 KB |

Generation: ~1.6e11 evaluations, roughly 1–2 h single-threaded or ~10 min across
8 cores. One-time offline `make tables` step; commit the resulting blob.

**Done when:** table values match known canonical matchups (AA vs KK preflop
≈82.36%, AKs vs QQ ≈46.0%, standard coin-flips), the generator is reproducible,
and a 40k-hand match against a shove-every-hand bot stays inside the wall-time
target.

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

1. Profile a full 40k-hand match; expected hotspots are the evaluator, live
   flop/turn equity enumeration, JSONL writing, and IPC round-trip cost.
2. Optimize against measurements only — no speculative tuning before this point.
3. Measure `--fork-per-hand` overhead against the default, and decide whether
   ranked matches feeding Elo should turn it on (see decisions table).
4. Run the full suite under ASan/UBSan and under Valgrind for the dlopen paths.
5. Long-run soak: several full matches, checking for leaks and drift.
6. Docs: README with build instructions, the bot-author guide (ABI, statelessness
   contract, timing contract, and exactly what the child-process sandbox does and
   does not cover), and the stat definitions from M7.
7. Resolve the two open items flagged at the bottom of SPEC.md (per-street stat
   list, Elo `k`).

**Done when:** default-config match completes in target time on the reference
machine, sanitizers are clean, and a new bot can be written from the docs alone.

---

## Cross-cutting decisions to settle early

| Decision | Why it's urgent | Recommendation |
|---|---|---|
| ~~Card encoding~~ | ~~Touches every component~~ | **Settled: rank-major, `c >> 2` / `c & 3` (M0)** |
| ~~Evaluator backend~~ | ~~`third_party/` and load-time cost~~ | **Settled: OMPEval (M1)** |
| ~~In-process vs child-process bots~~ | ~~Reshapes M5~~ | **Settled: child process per bot (M5)** |
| ~~`create_bot()` per hand vs init hook~~ | ~~Bot-author contract~~ | **Settled: once per match; `--fork-per-hand` for hard enforcement (M3)** |
| ~~Side pots~~ | ~~Engine complexity~~ | **Settled: impossible with equal stacks; assert the invariant (M2)** |
| Preflop table: flat 14 MB vs isomorphic 376 KB | Affects the M6 lookup path | Start flat; compress only if load time bites |
| Does `--fork-per-hand` default on for ranked/Elo matches | Policy, not code | Decide at M10 with real timings |
| ~~Odd-chip rule on chopped pots~~ | ~~Silent 1-chip bias~~ | **Settled: odd chip to the BB (M2)** |
| `--hands` odd with duplicate on | Breaks pairing | Error out rather than silently truncate |

## Deliberately out of scope

Elo (separate tool, `elo/`), multiway play, tournament/ICM, variable stack depths,
bot-vs-human play, and any web/visualization layer.

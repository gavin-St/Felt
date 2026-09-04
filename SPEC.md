# Felt specification (v5)

Felt is a macOS-first harness for comparing simple, stateless heads-up no-limit
Hold'em bots. Version 1 optimizes for short iteration time and a small codebase.
Bots are trusted: isolation for untrusted third-party submissions is future work.

The detailed poker, dealing, and random-number rules are in
[GAME_RULES.md](GAME_RULES.md).

## Platform and process model

- C++17 harness, built with CMake on 64-bit macOS 12 or newer.
- `run_match` forks a supervised worker. The worker plays the match and calls
  bots directly in its own process; the parent only supervises.
- Each bot is a dynamic library (`.dylib`) loaded with `dlopen` and
  `RTLD_NOW | RTLD_LOCAL`.
- The engine talks to an internal bot-runner interface. V1 supplies a native
  direct-call runner; this keeps transport details out of the poker engine.
- The supervisor makes bot hangs and crashes survivable at the harness level:
  it ends the match, records why in `summary.json`, and exits with a
  distinguishing code. It is a liveness guard, not a sandbox — a bot still runs
  as loaded code inside the worker, with the worker's full privileges. Do not
  run untrusted libraries.
- A later isolated runner may put bots in child processes without changing the
  public bot API.

Direct calls have negligible overhead compared with the default 2 ms decision
budget. They also keep the first implementation portable within macOS: no
Linux-only futex, seccomp, or CPU-affinity code is required.

## Bot API

The public header is valid C and contains only fixed-width integers, integer
constants, pointers, and plain structs. No C++ class, virtual method,
`std::string`, container, exception, or allocator crosses the boundary. Action,
street, and position codes are `uint32_t` values rather than C enums, whose
storage size is implementation-defined.

Every bot exports three C symbols:

```c
uint32_t felt_bot_abi_version(void);
const char *felt_bot_name(void);
FeltAction felt_bot_act(const FeltGameState *state);
```

- `felt_bot_abi_version()` must equal the harness ABI version.
- `felt_bot_name()` returns a process-lifetime, null-terminated name containing
  1–127 bytes.
- `felt_bot_act()` must not retain pointers from `state` after it returns.
- Load failure, a missing symbol, or an ABI mismatch aborts before the match and
  is not recorded as a forfeit.
- A C++ bot is allowed, but it must provide these C wrappers and must not let an
  exception cross them.

There is deliberately no bot object and no create/destroy lifecycle. Bots are
strategies expressed as functions. Expensive immutable lookup tables may be
compiled into the library or initialized internally once.

## Game state

`FeltGameState` exposes only information available to the acting player:

- two private hole cards;
- the visible board prefix and `board_count`; unused board slots contain the
  invalid-card value `255`;
- street and position (`0 = BTN/SB`, `1 = BB`);
- pot, including every chip currently committed;
- both remaining stacks;
- both players' current-street contributions;
- `to_call`, capped at the acting player's remaining stack;
- `min_raise_to` and `max_raise_to`;
- a legal-action bitmask;
- the complete public action history for this hand;
- the constant configured decision cap and an opaque per-decision
  `decision_random` value.

The history is exposed as a read-only pointer plus count because calls are
in-process. Each event records position, street, normalized action, and the
resulting total contribution on that street. Forced small- and big-blind posts
are explicit history events. The pointer is valid only during the call.

All chip fields and action amounts are signed 64-bit integers. Negative chip
values are never valid.

## Actions and validation

Bots return one of four actions:

- `FOLD`
- `CHECK`
- `CALL`
- `RAISE_TO`

`RAISE_TO.amount_to` is the player's desired **total contribution on the current
street**, not the number of additional chips to add. It represents both an
opening bet and a raise. Going all-in is not a separate action: use
`RAISE_TO{max_raise_to}`, or `CALL` when the call consumes the stack. The amount
field is ignored for fold, check, and call.

The legal-action mask is authoritative. When a short all-in raise is legal but
smaller than a full raise, `max_raise_to < min_raise_to`; in that case the only
legal aggressive amount is exactly `max_raise_to`.

`FOLD` and `CALL` are legal only when `to_call > 0`; `CHECK` is legal only
when `to_call == 0`. When `RAISE_TO` is not legal, both raise-to bounds are
zero. Otherwise a full raise may use any integer amount in the inclusive
`[min_raise_to, max_raise_to]` range.

An illegal action or amount becomes `CHECK` if check is legal, otherwise
`FOLD`, and a violation is logged. Amounts are not clamped: silently changing a
bet size would hide bot bugs.

## Statelessness and randomness

Bots must implement a pure strategy:

- no opponent identity, hand index, previous-hand result, or match score is
  provided;
- all information needed about the current hand is present in `FeltGameState`;
- any randomized choice must be derived solely from `decision_random` and the
  state;
- bots are single-threaded in v1.

This is a trusted contract, not a security guarantee. The harness does not try
to detect globals, filesystem state, clocks, or deliberate introspection. A
future untrusted-submission mode can use one isolated process per bot or hand.
The match and deal seeds are not exposed through the bot API;
`decision_random` is a one-way-derived value for reproducible bot choices, not
a seed that can be used to reproduce the deck.

## Timing

For each call, the harness records:

- bot-thread CPU time using `CLOCK_THREAD_CPUTIME_ID`;
- elapsed wall time using `CLOCK_MONOTONIC`.

The default CPU decision cap is 2 ms. After the call returns, an action whose
measured thread CPU time is greater than the cap is replaced with the normal
default action and a cap violation is logged. The requested action and timing
remain in the record. Cap violation takes precedence if the returned action is
also invalid.

The CPU cap is measured after the call returns, so it cannot by itself stop a bot
that never returns. That is the supervisor's job. The worker reports the start
and end of every decision to the parent over a pipe; if a decision exceeds
`--hard-timeout-ms` of wall time (default 1000 ms), the parent kills the worker,
marks the summary `aborted` with reason `decision_wall_timeout` plus the hand,
decision, bot, position and street, and exits **124**.

The two limits do different jobs and neither replaces the other. The CPU cap is
the fairness rule: charged only for the bot's own compute, so machine load never
costs a bot its action, and an overrun is self-punishing rather than fatal. The
wall timeout is the liveness rule: generous, wall-clock, and terminal.

An aborted match is not a forfeit and not a result. Hands completed before the
abort stay in the stream for inspection, but the match cannot be finalized into
the ledger.

Exit codes:

| Code | Meaning |
|---|---|
| 0 | match completed |
| 1 | setup error, or supervision failed (`supervisor_protocol_error`) |
| 124 | hard wall timeout during a decision (`decision_wall_timeout`) |
| 128 + N | worker killed by signal N, such as a bot crash (`worker_signal`) |

Timing measurements are observational and naturally vary between runs. Felt
guarantees reproducible deals from a seed, not byte-identical logs or necessarily
identical results from bots operating exactly at the timing boundary.

## Match format and scoring

- Heads-up NLHE, no ante and no rake.
- Defaults: blinds 50/100 and equal 20,000-chip stacks, reset every hand.
- Default length: 20,000 hands, meaning 10,000 adjacent duplicate pairs.
- With duplicate play enabled, `--hands` must be even.
- There is no stopping rule.
- Headline chips are net chips won, not final-stack totals.
- `bb/100 = net_chips / big_blind / hands * 100`.
- Adjusted chip winnings determine the match winner. Raw runout results are
  retained for inspection and conventional win/loss/showdown statistics.

When both players are all-in before the river, the pot is awarded from exact
enumerated equity. The rational payout is rounded down and the remaining chip is
given to the BB, matching the normal odd-chip rule. Duplicate play cancels that
positional rounding bias across each pair. The actual runout is still dealt and
logged.

## Statistics

Per bot and match:

- net adjusted chips, bb/100, and raw hands won/lost/chopped;
- 169 starting-hand buckets split by position (338 rows);
- 1,326 exact combinations split by position (2,652 rows);
- VPIP, PFR, raw win percentage, and showdown percentage;
- street action counts and fractions;
- c-bet, WTSD, and W$SD;
- all-in reached and initiated rates, split by street;
- showdown and non-showdown winnings;
- position bb/100;
- mean, p99, and maximum CPU and wall time, plus cap and illegal-action counts.
- raw and adjusted standard deviation and standard error, using duplicate-pair
  totals as the independent observations when duplicate play is enabled.

Definitions:

- **VPIP:** voluntarily calls or raises preflop; blind posts do not count, while
  completing the small blind does. Denominator: hands dealt.
- **PFR:** makes at least one preflop raise. Denominator: hands dealt.
- **C-bet:** bets the flop as the last preflop aggressor when first to act or
  checked to. Denominator: such flop opportunities.
- **WTSD:** reaches showdown. Denominator: hands that saw a flop.
- **Raw win percentage:** wins the dealt hand, including wins by fold.
  Denominator: hands dealt in that row.
- **Showdown percentage:** reaches showdown. Denominator: hands dealt in that
  row.
- **W$SD:** wins the raw runout at showdown; a chop is not a win. Denominator:
  showdowns reached.
- Street action fractions use that bot's decisions on that street as the
  denominator.
- **All-in reached:** betting ends with both players having committed their full
  effective stacks. Denominator: hands dealt. Record the street on which this
  became inevitable.
- **All-in initiated:** the bot made the first action that committed its full
  effective stack; a subsequent all-in call is a response, not an initiation.
  Denominator: hands dealt.

Chip results and bb/100 use equity-adjusted winnings. Win/loss/chop, showdown,
and W$SD use the actual runout, so those counts intentionally need not imply the
adjusted chip result.

## Output and replay

During play, the output directory contains `summary.json` and a streaming
`hands.jsonl`. The hand stream is the authoritative record of what occurred;
the summary carries the schema and harness versions, complete match
configuration, bot hashes, and aggregate results.

After a completed match, a finalizer transaction imports both into the local
Git-ignored `data/felt.sqlite3`. It stores searchable normalized facts plus the
exact JSON hand records in compressed chunks, validates totals, then calculates
derived statistics with SQL. Only after the transaction commits may it remove
the temporary hand stream. Statistics can therefore be rebuilt later entirely
from the database.

The database contains per-bot results, positions, poker rates, all-in and pot
classes, actions, timings, violations, paired uncertainty, 169-bucket and exact
combination profitability, and one searchable row per bot perspective per hand.
An exporter reconstructs `summary.json` and `hands.jsonl` when full inspection
or replay is needed. The exact schema and commands are documented in
[LOG_FORMAT.md](LOG_FORMAT.md).

Replay from logged cards and actions must reconstruct the hand exactly. Rerunning
bots from the match seed is a separate diagnostic and may differ because timing
is part of action acceptance.

## Round robin and hand exploration

The later rating tool runs every unordered bot pairing and keeps each match in a
unique directory. Bots are identified by library hash, and results are grouped
by an exact rules profile; matches with different stacks, blinds, decision caps,
duplicate settings, or equity settings are never silently combined.

The primary view is a square bot-versus-bot matrix ordered by Elo. A cell shows
the row bot's adjusted bb/100 against the column bot, total hands, and
uncertainty. Color uses a zero-centered win/loss scale. When several compatible
matches exist for a pairing, combine chip totals and hand counts before
calculating bb/100 rather than averaging per-match rates.

Selecting a cell shows match summaries, raw and adjusted results, all-in rates,
standard poker statistics, position splits, timing and violation statistics,
and the most and least profitable starting-hand buckets. Bucket rows include
sample count, total raw and adjusted BB, and normalized bb/100; use canonical
rank-first labels such as `76s`. Exact two-card combinations remain available
as a finer view.

The hand browser treats each hand from each bot's perspective as a searchable
record. Filters include bot and opponent, match, 169 bucket, exact cards,
position, whether the flop was seen, showdown or fold, final pot size in BB,
raw/adjusted outcome, and all-in street and initiator. Preflop pot class is based
on voluntary raise count:

- walk: the hand ends preflop with no voluntary raise;
- limped/unraised: no voluntary raise and a flop is seen;
- single-raised pot: one voluntary preflop raise;
- 3-bet pot: two voluntary preflop raises;
- 4-bet+ pot: three or more voluntary preflop raises.

Pot class and all-in status are independent: for example, a hand may be both a
3-bet pot and all-in on the flop. The browser supports paginated matching hands,
previous/next, and uniform random selection under the current filters.

The local SQLite ledger is authoritative after finalization. It stores exact
compressed JSON histories alongside normalized searchable facts. A stable
random key per perspective row lets random selection use an index instead of
`ORDER BY RANDOM()` on millions of rows.

The v1 local-storage budget is 10 GB. Felt must never silently discard old
matches to stay below it; a later scheduler/UI should report database size and
warn before a planned round robin would exceed the budget.

## CLI

```text
run_match botA.dylib botB.dylib \
  --hands 20000 \
  --seed 123 \
  --stack 20000 --sb 50 --bb 100 \
  --decision-cap-ms 2 \
  --hard-timeout-ms 1000 \
  --no-duplicate \
  --no-equity-adjust \
  --out ./results/
```

Duplicate play and equity adjustment are on by default. A slower search bot can
be tested with, for example, `--decision-cap-ms 500 --hard-timeout-ms 5000
--hands 3000` (use an even hand count while duplicate play is enabled). Keep the
hard timeout comfortably above the decision cap: the cap governs a bot's own
compute, while the timeout has to absorb scheduling and page-fault noise too.

## Later, separate work

- Process isolation and sandboxing for untrusted submissions.
- Resuming a match after an aborted decision instead of ending it, which needs a
  bot-per-process runner so one bot can be restarted without losing the other.
- A persistent Python worker runner. It will start one interpreter per bot per
  match and exchange states/actions over a versioned process protocol; it will
  never start Python once per decision or hand. A readable JSON-lines protocol
  can come first, followed by a binary transport only if profiling requires it.
- Multiway poker, tournaments/ICM, and unequal starting stacks.

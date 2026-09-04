# Felt specification (v5)

Felt is a macOS-first harness for comparing simple, stateless heads-up no-limit
Hold'em bots. Version 1 optimizes for short iteration time and a small codebase.
Bots are trusted: isolation for untrusted third-party submissions is future work.

The detailed poker, dealing, and random-number rules are in
[GAME_RULES.md](GAME_RULES.md).

## Platform and process model

- C++17 harness, built with CMake on 64-bit macOS 12 or newer.
- One `run_match` process. Bots run in that process and are called directly.
- Each bot is a dynamic library (`.dylib`) loaded with `dlopen` and
  `RTLD_NOW | RTLD_LOCAL`.
- The engine talks to an internal bot-runner interface. V1 supplies a native
  direct-call runner; this keeps transport details out of the poker engine.
- There is no sandbox in v1. A bot crash or infinite loop can terminate or hang
  the match. Do not run untrusted libraries.
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

The default CPU decision cap is 2 ms. After the call returns, an over-cap action
is replaced with the normal default action and a cap violation is logged. This
is intentionally simple, but it cannot interrupt an infinite loop. A hung trusted
bot must be stopped with normal process controls; recoverable hard timeouts are
deferred to the future isolated runner.

Timing measurements are observational and naturally vary between runs. Felt
guarantees reproducible deals from a seed, not byte-identical logs or necessarily
identical results from bots operating exactly at the timing boundary.

## Match format and scoring

- Heads-up NLHE, no ante and no rake.
- Defaults: blinds 50/100 and equal 20,000-chip stacks, reset every hand.
- Default length: 40,000 hands, meaning 20,000 adjacent duplicate pairs.
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

The output directory contains:

- `summary.json`
- `hand_stats.csv`
- `combo_stats.csv`
- `hands.jsonl`

The versioned field-level contract and verification commands are documented in
[LOG_FORMAT.md](LOG_FORMAT.md).

`hands.jsonl` is the authoritative record of what occurred. Each hand contains
its pair/hand identifiers, deal seed, both hole cards, full board, normalized
actions, per-decision CPU and wall times, violations, and raw and adjusted
results. `summary.json` includes a schema version, complete match configuration,
harness version, and hashes of both bot libraries.

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

`hands.jsonl` remains authoritative. A compact SQLite catalog stores searchable
facets and the corresponding JSONL byte offset rather than duplicating complete
histories. Store a stable random key per perspective row so random selection can
use an index instead of `ORDER BY RANDOM()` on millions of rows.

## CLI

```text
run_match botA.dylib botB.dylib \
  --hands 40000 \
  --seed 123 \
  --stack 20000 --sb 50 --bb 100 \
  --decision-cap-ms 2 \
  --no-duplicate \
  --no-equity-adjust \
  --out ./results/
```

Duplicate play and equity adjustment are on by default. A slower search bot can
be tested with, for example, `--decision-cap-ms 500 --hands 3000` (use an even
hand count while duplicate play is enabled).

## Later, separate work

- Process isolation and sandboxing for untrusted submissions.
- Recoverable CPU and wall-clock timeouts.
- A persistent Python worker runner. It will start one interpreter per bot per
  match and exchange states/actions over a versioned process protocol; it will
  never start Python once per decision or hand. A readable JSON-lines protocol
  can come first, followed by a binary transport only if profiling requires it.
- Multiway poker, tournaments/ICM, and unequal starting stacks.

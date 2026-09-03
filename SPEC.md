# HU NLHE Bot Harness — Condensed Spec (v4)

## Language / architecture

- C++17+, single binary `run_match`. One match = one process.
- Bots are shared libraries loaded via `dlopen`, exporting `extern "C" Bot* create_bot()`.
- In-process interface: abstract `Bot` class with `name()` and `Action act(const GameState&)`. No match/hand lifecycle hooks.
- `GameState` (const ref): `hole[2]`, `board[5]`, `board_count`, `street`, `position` (0 = BTN/SB, 1 = BB), `pot`, `my_stack`, `opp_stack`, `to_call`, `min_raise_to`, `max_raise_to`, action history (this hand only), `decision_cap_us` (constant), per-decision `rng_seed` (bots must use it for any randomness).
- `Action`: `{Fold, Check, Call, Bet, Raise, AllIn}` + amount. Harness validates; illegal → default action + logged violation.
- Cards = `uint8` 0–51. 7-card evaluator: OMPEval or 2+2 lookup.
- Bots never see opponent cards; harness owns all RNG and dealing.

## Statelessness (enforced)

- Bots receive no hand index, opponent identity, or results — `act()` is a pure function of `GameState`.
- Bot object destroyed and recreated via `create_bot()` every hand.
- Optional check: replay 1% of hands with identical `GameState` → identical actions; mismatch → match invalidated.
- No wall-clock access. `decision_cap_us` is a constant config value, not a running clock, so there is no time budget to reason about strategically.

## Game format

- No-Limit Hold'em, heads-up only. Two bots per invocation.
- Blinds 50/100, stacks reset to 20,000 (200 bb) every hand.
- Correct NLHE rules: min-raise = last raise size, all-in for less doesn't reopen action, short all-in handled.
- Duplicate poker: every deal played twice with seats swapped. Deal seed = `hash(match_seed, hand_index)`.
- All-in equity adjustment: both all-in before river → award pot × exact equity (enumerate remaining boards). Runout still dealt for logs.

## Match length

- Default 40,000 hands (20,000 duplicate pairs). No stopping rule; winner = higher total equity-adjusted chips.
- Target wall time 7–15 min at default cap.

## Timing (all configurable via CLI)

Two clocks, each with one job. **No time bank** — a bank would make timing a
strategic resource that bot authors must reason about, which has nothing to do
with poker.

- **Decision cap — CPU time, default 2 ms.** Measured with
  `CLOCK_THREAD_CPUTIME_ID` inside the bot's process, so scheduling delays and
  machine load are not charged to the bot. Exceeding it → default action (check
  if legal, else fold) + logged violation. This is **self-punishing**: a bot that
  overruns gets a fold instead of its intended action and pays for it in chips,
  so no further penalty is applied and slow decisions never forfeit.
- **Hard ceiling — wall clock, default 200 ms.** Measured in the parent, which a
  bot cannot influence. This is the liveness check, not a fairness check: it
  fires only on genuine hangs. → default action + hard violation.
- 3 hard-ceiling violations, or exception/crash → forfeit.

Using CPU time for the cap and wall time for the ceiling means the two cover each
other: a bot that spins is caught by the CPU cap, and one that sleeps or blocks
(costing no CPU time) is caught by the wall ceiling.

Each bot runs in its own **child process** (shared memory + futex transport), so
a crash or hang is survivable and the harness can still write results — the
spec's own "crash → forfeit" rule requires this. Limits are applied in the child
between fork and dlopen: 1 core (affinity), 1 GB (`RLIMIT_AS`), and no network
(seccomp filter on the socket syscalls, so this is enforced rather than
contractual).

## Scoring & stats (per bot, per match)

- Headline: total chips, bb/100, hands won / lost / chopped.
- Per starting hand (169 buckets): dealt, VPIP %, PFR %, win %, showdown %; split by position → 338 rows.
- Per exact combo (1,326): same fields, CSV.
- Per-street: fold / check / call / bet / raise frequencies, c-bet %, WTSD, W$SD.
- Per-position: bb/100 as BTN vs BB.
- Timing: mean / p99 / max decision time (both CPU and wall), cap and ceiling violation counts.
- Output: `summary.json`, `hand_stats.csv`, `combo_stats.csv`.

## Logging / reproducibility

- Per-hand JSONL: `hand_index`, `seed`, both holes, board, full action sequence with per-decision timing, raw and adjusted result (~8 MB/match).
- `replay <hand_index>` CLI reproduces any hand.

## CLI

```
run_match botA.so botB.so \
  --hands 40000 \
  --seed 123 \
  --stack 20000 --sb 50 --bb 100 \
  --decision-cap-ms 2 \
  --hard-ceiling-ms 200 \
  --max-hard-violations 3 \
  --no-duplicate            # optional, default on
  --no-equity-adjust        # optional, default on
  --verify-stateless 0.01   # fraction of hands replayed
  --out ./results/
```

Example for search-based bots: `--decision-cap-ms 500 --hands 3000`.

**Choosing `--hands` and `--decision-cap-ms`.** Worst case wall time is
`hands x ~6 decisions x cap` (6 decisions per hand across both bots is typical).
Most bots use a fraction of their cap, so this is a ceiling, not a forecast:

| hands | 2 ms | 3 ms | 5 ms | resolution (bb/100, 2 sigma) |
|---|---|---|---|---|
| 20,000 | 4 min | 6 min | 10 min | ~7.0 |
| 30,000 | 6 min | 9 min | 15 min | ~5.8 |
| **40,000** | **8 min** | 12 min | 20 min | **~5.0** |
| 60,000 | 12 min | 18 min | 30 min | ~4.0 |

The defaults (40,000 hands at 2 ms) sit inside the 7-15 min target with headroom.
A 2 ms budget affords roughly 60,000 hand evaluations, which is ample for Monte
Carlo rollouts; it is not enough for real-time subgame solving, and is not meant
to be.

## Elo (later, separate tool)

- Ledger of match results (SQLite/JSON).
- Margin → score: `s = 1 / (1 + e^(-m/k))`, where `m` = bb/100 and `k` is a scale constant (≈10 bb/100 as a starting point).

---

### Open items

The source text was truncated in a few places; these were reconstructed and should be confirmed:

- The exact per-street stat list (`c-bet %`, `WTSD`, `W$SD` assumed).
- The Elo margin scale constant `k`.

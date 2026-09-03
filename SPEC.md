# HU NLHE Bot Harness — Condensed Spec (v4)

## Language / architecture

- C++17+, single binary `run_match`. One match = one process.
- Bots are shared libraries loaded via `dlopen`, exporting `extern "C" Bot* create_bot()`.
- In-process interface: abstract `Bot` class with `name()` and `Action act(const GameState&)`. No match/hand lifecycle hooks.
- `GameState` (const ref): `hole[2]`, `board[5]`, `board_count`, `street`, `position` (0 = BTN/SB, 1 = BB), `pot`, `my_stack`, `opp_stack`, `to_call`, `min_raise_to`, `max_raise_to`, action history (this hand only), `time_bank_us`, per-decision `rng_seed` (bots must use it for any randomness).
- `Action`: `{Fold, Check, Call, Bet, Raise, AllIn}` + amount. Harness validates; illegal → default action + logged violation.
- Cards = `uint8` 0–51. 7-card evaluator: OMPEval or 2+2 lookup.
- Bots never see opponent cards; harness owns all RNG and dealing.

## Statelessness (enforced)

- Bots receive no hand index, opponent identity, or results — `act()` is a pure function of `GameState`.
- Bot object destroyed and recreated via `create_bot()` every hand.
- Optional check: replay 1% of hands with identical `GameState` → identical actions; mismatch → match invalidated.
- No wall-clock access other than `time_bank_us`.

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

- Default 2 ms per decision soft cap; overage drawn from 10 s per-match time bank.
- Bank empty and over cap → default action (check if legal, else fold) + violation.
- Default 200 ms hard ceiling per decision → default action.
- 3 hard-ceiling violations, or exception/crash → forfeit.
- Bot runs on a dedicated worker thread; harness waits with a `steady_clock` deadline and discards late results.
- Limits: 1 core, 1 GB, no network.

## Scoring & stats (per bot, per match)

- Headline: total chips, bb/100, hands won / lost / chopped.
- Per starting hand (169 buckets): dealt, VPIP %, PFR %, win %, showdown %; split by position → 338 rows.
- Per exact combo (1,326): same fields, CSV.
- Per-street: fold / check / call / bet / raise frequencies, c-bet %, WTSD, W$SD.
- Per-position: bb/100 as BTN vs BB.
- Timing: mean / p99 / max decision time, bank used, soft/hard violations.
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
  --time-bank-s 10 \
  --hard-ceiling-ms 200 \
  --max-hard-violations 3 \
  --no-duplicate            # optional, default on
  --no-equity-adjust        # optional, default on
  --verify-stateless 0.01   # fraction of hands replayed
  --out ./results/
```

Example for search-based bots: `--decision-cap-ms 500 --time-bank-s 60 --hands 3000`.

## Elo (later, separate tool)

- Ledger of match results (SQLite/JSON).
- Margin → score: `s = 1 / (1 + e^(-m/k))`, where `m` = bb/100 and `k` is a scale constant (≈10 bb/100 as a starting point).

---

### Open items

The source text was truncated in a few places; these were reconstructed and should be confirmed:

- The exact per-street stat list (`c-bet %`, `WTSD`, `W$SD` assumed).
- The Elo margin scale constant `k`.

# always_all_in

Shoves every hand, from either seat, with any two cards.

## What it is for

**A betting-engine oracle.** Paired with `check_fold` it produces a hand shape
with a known answer, which is why the plan uses that pairing to validate the
match runner rather than `check_fold` versus `check_call` — the latter can check
through to showdown and is not an oracle.

**The pathological case for equity enumeration.** It forces a preflop all-in
every hand, which is the expensive branch of the exact-equity code: 1,712,304
boards per novel matchup against 990 on the flop. If the memoised preflop cache
in `solvers`/`equity` is ever too slow, this bot is what exposes it.

## Measured

| Opponent | Result for the opponent |
|---|---|
| `check_call` | 0.0 bb/100 — exact cancellation, see that bot's README |
| `nit_all_in` | +85.1 bb/100 |
| `solved_all_in` | +194.4 bb/100 |
| `better_all_in` | +1284.6 bb/100 |

Everything beats it, but the spread across those four is the interesting part.
See `solved_all_in`'s README.

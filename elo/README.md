# elo

Separate tool (built later). Keeps a ledger of match results (SQLite or JSON)
and turns margins into ratings.

Margin → score: `s = 1 / (1 + e^(-m/k))`, where `m` is bb/100 and `k` is a
scale constant to be tuned.

Planned layout:

```
elo/
  ledger/          # match result store
  src/             # rating computation, leaderboard output
```

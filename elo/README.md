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

## Precision constraint (read before designing ratings)

A default 20,000-hand match resolves differences of only about **7 bb/100** at
two sigma; separating bots that differ by 1 bb/100 would need on the order of a
million hands. Ratings must therefore carry an uncertainty interval rather than
reporting a point estimate, and the ledger should record hands played per match
so confidence can be computed rather than assumed.

The ledger must identify bots by library hash, not display name, and must not mix
matches from different rules profiles (stack, blinds, decision cap, duplicate
mode, or equity adjustment). For duplicate matches, uncertainty should be
estimated from paired-hand results rather than treating all hands as independent.

Derivation and the AIVAT variance-reduction option: [../PRIOR_ART.md](../PRIOR_ART.md).

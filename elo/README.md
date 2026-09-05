# Ratings

`scripts/rebuild_ratings.py` turns the SQLite ledger's raw match results into
an uncertainty-aware ordering for the matrix.

Ratings are direction-first. A positive raw match result contributes one
logistic rating unit, while its size contributes only a bounded bonus:
`sign(m) * (1 + 0.15 * tanh(abs(m) / k))`, where `m` is raw bb/hand and the
default `k` is 1 bb/hand. A win is therefore the main signal and even an
extreme margin can increase its rating effect by at most 15%. The result is
mapped to the standard Elo scale and the zero point is fixed at 1500.

Each standard 20,000-hand matchup has equal rating weight, preventing a
deterministic matchup from overwhelming the win/loss graph merely because its
raw result has near-zero variance. Its 95% intervals are widened when the
observed matchup graph is more non-transitive than sampling error explains.
Disconnected graph components are numbered separately and are not comparable.
Version 1 rejects repeated pairings within one rules profile rather than
aggregating them.

## Precision constraint (read before designing ratings)

A default 20,000-hand match resolves differences of only about **0.07 bb/hand**
at two sigma; separating bots that differ by 0.01 bb/hand would need roughly a
million hands. Ratings must therefore carry an uncertainty interval rather than
reporting a point estimate, and the ledger should record hands played per match
so confidence can be computed rather than assumed.

The ledger identifies bots by library hash, not display name, and never fits
matches from different rules profiles together (stack, blinds, decision cap,
duplicate mode, or equity adjustment). Duplicate matches estimate uncertainty
from paired-hand results rather than treating all hands as independent.

Derivation and the AIVAT variance-reduction option: [../PRIOR_ART.md](../PRIOR_ART.md).

# slp-fold

Uses Felt's shared `baseline_100bb_v1` chart preflop. Postflop it bets 75% pot
with top pair, an overpair, over two pair, both-hole-card two pair, or trips and
better. It raises those hands to three times the opponent's total street
contribution (all-in when shorter). It checks or calls with a smaller pair,
under or middle two pair, or a live flop/turn draw. Board-only two pair is
treated like air and checks or folds.

A missed draw is air on the river.

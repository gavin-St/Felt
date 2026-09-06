# slp-bluff

Uses Felt's shared `baseline_100bb_v1` chart preflop. Postflop it bets 75% pot
with top pair, an overpair, over two pair, both-hole-card two pair, trips and
better, and with air. It raises those hands to three times the opponent's total
street contribution (all-in when shorter). It checks or calls with a smaller
pair, under or middle two pair, or a live flop/turn draw. Board-only two pair
is treated like air, so this profile bluffs it.

A missed draw is air on the river, so this version bluffs it.

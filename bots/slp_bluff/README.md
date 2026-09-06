# slp-bluff

Uses Felt's shared `baseline_100bb_v1` chart preflop. Postflop it bets 75% pot
with top pair or better and with air, or raises to three times the opponent's
total street contribution (all-in when the stack is too short). It checks or
calls with a smaller pair or a live flop/turn draw.

A missed draw is air on the river, so this version bluffs it.

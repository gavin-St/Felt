# slp-balance

Uses Felt's shared `baseline_100bb_v1` chart preflop. Postflop it bets 75% pot
with top pair or better, or raises to three times the opponent's total street
contribution (all-in when the stack is too short). It checks or calls with a
smaller pair or a live flop/turn draw. With air, deterministic per-decision
randomness chooses the aggressive line 50% of the time and check/fold 50%.

A missed draw is air on the river and receives the same 50/50 choice.

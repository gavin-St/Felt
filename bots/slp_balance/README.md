# slp-balance

Uses Felt's shared `baseline_100bb_v1` chart preflop. With top pair or an
overpair and no bet to call, deterministic per-decision randomness checks 33%
and bets 75% pot 67%. Facing either an opening bet or a raise, a single top pair
or overpair always calls and never reraises. Every hole-card-improved two pair
checks or calls and never raises. Trips or better calls or checks 33% and raises
or bets 67%, using a fresh per-decision random value each time.

Under and middle two pair count as two pair when a hole card improves the board;
board-only two pair is treated like air. Smaller pairs and live draws call an
opening bet. Facing a raise, smaller pairs fold and draws call 33%. With air,
independent per-decision randomness chooses bet or check with equal probability
when action is free and always folds to aggression.

A missed draw is air on the river: it may bluff when checked to but folds to
aggression.

# slp-balance

Uses Felt's shared `baseline_100bb_v1` chart preflop. With top pair or better
and no bet to call, deterministic per-decision randomness checks 33% and bets
75% pot 67%. Facing aggression, one-pair top pairs and overpairs always call;
two pair or better calls 33% and raises to three times the opponent's total
street contribution 67%. It checks or calls with a smaller pair or live
flop/turn draw unless the additional call exceeds the pot before the action,
in which case it folds. With air, independent per-decision randomness chooses
bet or check with equal probability when action is free. It always folds air
when facing a bet or raise.

A missed draw is air on the river: it may bluff when checked to but folds to
aggression.

# better_all_in

Shoves a union of "premium" categories, folds everything else:

- pocket pairs 99 and better;
- any two broadway cards (T, J, Q, K, A);
- any ace;
- any king.

**452 of 1,326 combinations — 34.1% of hands.**

Ace-x and king-x dominate that union, so it is far wider than "premium"
suggests: the full A and K rows are in, meaning it shoves K2o and A3o. The
broadway and 99+ clauses only add QJ, QT, JT and the 99 diagonal on top.

```
     A  K  Q  J  T  9  8  7  6  5  4  3  2      (above the diagonal = suited)
  A  #  #  #  #  #  #  #  #  #  #  #  #  #
  K  #  #  #  #  #  #  #  #  #  #  #  #  #
  Q  #  #  #  #  #  .  .  .  .  .  .  .  .
  J  #  #  #  #  #  .  .  .  .  .  .  .  .
  T  #  #  #  #  #  .  .  .  .  .  .  .  .
  9  #  #  .  .  .  #  .  .  .  .  .  .  .
  8  #  #  .  .  .  .  .  .  .  .  .  .  .
```

## Decision rule

When its hole cards fall in the displayed range, raise to
`state->max_raise_to`, or call if raising is unavailable because the opponent is
already all-in. Otherwise check when free and fold when facing a bet. The same
range is used from both positions.

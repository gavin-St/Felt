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

## Measured

Crushes `always_all_in` by **+1284.6 bb/100**, the largest margin in the
reference set — a wide-but-selective range against a range of literally any two
is an enormous edge.

Loses to `solved_all_in` by 50.6 bb/100, which is the more interesting result.
See [`../solved_all_in`](../solved_all_in).

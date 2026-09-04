# worse_all_in

Shoves only bad hands. The range is the complement of everything worth playing:

- no pocket pairs;
- no suited hands;
- no connectors or one-gappers (rank gap of at least 3);
- no aces or kings.

**432 of 1,326 combinations — 32.6% of hands**, every one offsuit, unpaired,
disconnected and headed by a queen or worse: Q9o down to 72o.

```
     A  K  Q  J  T  9  8  7  6  5  4  3  2      (above the diagonal = suited)
  A  .  .  .  .  .  .  .  .  .  .  .  .  .
  K  .  .  .  .  .  .  .  .  .  .  .  .  .
  Q  .  .  .  .  .  .  .  .  .  .  .  .  .
  J  .  .  .  .  .  .  .  .  .  .  .  .  .
  T  .  .  .  .  .  .  .  .  .  .  .  .  .
  9  .  .  #  .  .  .  .  .  .  .  .  .  .
  8  .  .  #  #  .  .  .  .  .  .  .  .  .
  7  .  .  #  #  #  .  .  .  .  .  .  .  .
  6  .  .  #  #  #  #  .  .  .  .  .  .  .
  5  .  .  #  #  #  #  #  .  .  .  .  .  .
  4  .  .  #  #  #  #  #  #  .  .  .  .  .
  3  .  .  #  #  #  #  #  #  #  .  .  .  .
  2  .  .  #  #  #  #  #  #  #  #  .  .  .
```

## How bad it is

Its shoving range averages **44.0% equity against a single random hand**,
measured over 400,000 sampled boards. It is committing 200 bb as an underdog to
literally any two cards, before the opponent gets to fold the worst of theirs.

## Decision rule

With a hand in the displayed junk range, raise to `state->max_raise_to`, or call
if the opponent is already all-in. With every other hand, check when free and
otherwise fold. The same deliberately inverted range is used from both
positions.

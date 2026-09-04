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

| Range | Combos | Equity vs a random hand |
|---|---|---|
| `nit_all_in` | 18 | 82.4% |
| `better_all_in` | 452 | 59.9% |
| any two cards | 1,326 | 50.1% |
| **`worse_all_in`** | **432** | **44.0%** |

## Why it is useful

It is a **controlled opposite** of [`../better_all_in`](../better_all_in). The
two shove almost the same *number* of hands — 32.6% against 34.1% — but
disjoint ones. Aggression frequency is held constant and only hand selection
differs, so any gap between them is purely selection, with nothing to argue
about regarding how often each is putting money in.

It also gives the reference set a floor. Every other bot should beat it
comfortably, and a rating tool that cannot separate this from `solved_all_in`
is not working.

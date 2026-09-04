# solvers

Offline tools that compute strategy tables for bots. They are not part of the
harness build and never run during a match: each produces a C header that is
committed alongside the bot that uses it.

## push_fold — 200 bb shove-or-fold

Solves the restricted game in which both players may only shove or fold
preflop, and emits `bots/solved_all_in/push_fold_table.h`.

**What it is not.** This is not heads-up NLHE. Real play at 200 bb happens
postflop, and the published heads-up push/fold Nash charts everyone links to are
solved for **10-20 bb**, where the game genuinely collapses into two decisions.
At 200 bb it does not. What this solves is the best a *pure shove-or-fold*
strategy can do at Felt's default depth, which is exactly what `solved_all_in`
is meant to represent — a ceiling for the all-in bot family, not a strong player.

### Method

1. A 169x169 preflop equity matrix by Monte Carlo, sampling disjoint
   combinations so card removal is handled, using the vendored OMPEval.
2. Fictitious play to an approximate equilibrium. Iterated best response
   oscillates in this game, so each side best-responds to the opponent's running
   average strategy instead.

Payoffs are in big blinds from the acting player's perspective, relative to the
start of the hand. With effective stack `S`, the pot when both are all in is
`2S`:

| Outcome | Payoff |
|---|---|
| SB folds | `-0.5` |
| SB shoves, BB folds | `+1.0` |
| both all in | `2S * equity - S` |
| BB folds to a shove | `-1.0` |

### Result at 200 bb

| Spot | Range | Combos |
|---|---|---|
| SB open shove | AA KK QQ AKs AKo AQs AJs ATs A5s A4s | 54 (4.1%) |
| BB shove over a limp | AA KK | 12 (0.9%) |
| BB continue vs a raise | AA KK QQ AKs | 22 (1.7%) |

Everything is this tight because shoving risks 200 bb to win 1.5 bb, so it needs
either enormous equity or enormous fold equity. A4s and A5s sit beside ATs rather
than A9s because wheel aces gain straight equity and block an ace-heavy calling
range — a real effect, not sampling noise.

Bets faced are bucketed into two cases, as the bot requires: anything at or
below the big blind is the limp case, and any raise is treated as facing a
shove, for which the equilibrium answer is already the calling range.

Many hands sit close to indifference at this depth, so the boundary of the SB
range moves slightly with the sample count. That is a property of the game, not
a defect — nothing near the boundary matters much in expectation.

### Building and running

Not wired into the main CMake build, since it is a host tool needed only when
regenerating the table:

```sh
c++ -O3 -std=c++17 \
  -I solvers/push_fold -I harness/third_party/ompeval \
  -o /tmp/solve_push_fold \
  solvers/push_fold/solve_push_fold.cpp \
  harness/third_party/ompeval/omp/HandEvaluator.cpp

/tmp/solve_push_fold 40000 bots/solved_all_in/push_fold_table.h
```

The first argument is Monte Carlo samples per matchup; 40,000 takes about a
minute and is stable enough. It also prints the four range grids for inspection.

Regenerating the table changes bot behaviour, so re-run the bot's range check
and commit the new header with the ranges it produces.

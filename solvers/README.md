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
| BB vs a limp/small raise (< 75 bb) | AA KK | 12 (0.9%) |
| BB vs a large raise/all-in (>= 75 bb) | AA KK QQ AKs | 22 (1.7%) |

Everything is this tight because shoving risks 200 bb to win 1.5 bb, so it needs
either enormous equity or enormous fold equity. A4s and A5s sit beside ATs rather
than A9s because wheel aces gain straight equity and block an ace-heavy calling
range — a real effect, not sampling noise.

### Why only the large/all-in bucket is solved

Facing an all-in needs no assumption: the solve already supplies the shoving
range, so the calling range is exact. The same response is used once a partial
raise reaches 75 bb. At the default 200 bb depth that commits 37.5% of the
stack, making it strategically much closer to an all-in than an ordinary open.
The 75 bb boundary is a documented heuristic, not an output of the solve.

Facing a *smaller* raise is not defined by this game at all — nobody makes a 2 bb
raise in it — so it needs an assumption about the raiser, and solving that as a
fixed point does not converge. Alternating best response oscillates between
"shove wide because they fold" and "shove tight because they call": across a
sweep of assumed raise sizes and opening frequencies the answer jumped between
0.5% and 29%, non-monotonically, with a 5 bb raise giving 1.4% against a 15%
opener and 19.3% against a 10% one. Those numbers are artefacts of which basin
the iteration landed in, not solutions.

Rather than preserve several partial-raise buckets that all produced AA/KK, the
bot deliberately groups every limp or raise below 75 bb into one response. This
keeps the approximation visible and avoids distinctions that do not change the
strategy.

### The small-raise response is tighter than the large/all-in response

That looks wrong and is not. Facing an all-in you are against the wide 4.1%
shoving range, so QQ and AKs are comfortably ahead. Re-shoving 200 bb over a 2 bb
raise wins 2 bb when it works and runs into a premium when it does not: called
with 33% equity costs 68 bb, against a 3 bb swing when it succeeds. Only AA and
KK survive that.

The deeper cause is a limitation of the bot family rather than of the solve.
Folding QQ to a min-raise is absurd in real poker — you would call. This bot
cannot call, so its only alternative to folding is a 200 bb shove, and against
that choice folding really is better. It is a fair illustration of why
shove-or-fold is the wrong shape of strategy at this depth.

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

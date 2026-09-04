# solved_all_in

The best a pure shove-or-fold strategy can do at 200 bb. Ranges come from
[`../../solvers/push_fold`](../../solvers/push_fold), which solves the
restricted game where both players may only shove or fold preflop.

| Spot | Range | Combos |
|---|---|---|
| SB open shove | AA KK QQ AKs AKo AQs AJs ATs A5s A4s | 54 (4.1%) |
| BB vs limp or a raise below 75 bb | AA KK | 12 (0.9%) |
| BB vs a raise to 75 bb+ or all-in | AA KK QQ AKs | 22 (1.7%) |

A4s and A5s sit beside ATs rather than A9s because wheel aces gain straight
equity and block an ace-heavy calling range — a real effect, not sampling noise.

Everything is this tight because shoving risks 200 bb to win 1.5 bb. Only the
large-raise bucket is genuinely solved; the smaller bucket uses a deterministic
rule, because responding to a raise needs an assumption about the raiser that
the restricted game never defines. The solver README has the detail.

## Measured, and the lesson in it

| Matchup | Result |
|---|---|
| beats `better_all_in` | **+50.6 bb/100** |
| beats `always_all_in` | +194.4 bb/100 |

But `better_all_in` beats `always_all_in` by **+1284.6 bb/100** — more than six
times this bot's margin against the same opponent.

That is not a contradiction, it is the whole point of an equilibrium strategy.
These ranges are the correct answer to an opponent playing the *solved* 4.1%
shoving range. Against a bot that shoves 100%, folding 98.3% of the time is
badly wrong: you are being offered a coin-flip against a random hand and
declining it, bleeding blinds instead. `better_all_in` accepts, and prints.

**Unexploitable is not the same as maximally exploitative.** This bot cannot be
beaten badly by anything, and cannot beat anything badly either. It is the
ceiling for the shove-or-fold family and the floor for what a postflop bot
should aim at — beating it only requires being willing to play a flop.

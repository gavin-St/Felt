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

On the button it uses the SB open range. In the big blind it reads the posted
blind from action history, applies the AA/KK response below 75 bb, and switches
to AA/KK/QQ/AKs at 75 bb or when the opponent has no chips behind. It gives up
postflop whenever a checked big blind reaches a board.

# seeded_random

Picks uniformly among the legal actions, deriving every choice from
`state->decision_random`.

Because it chooses among *legal* actions rather than hard-coding splits, the
resulting distribution is what you would want to specify by hand. Measured over
300,000 decisions per spot:

| Situation | Distribution |
|---|---|
| Facing a bet | fold 33.3% / call 33.3% / raise 33.3% |
| No bet to face | check 49.9% / raise 50.1% |
| Facing an all-in | fold 49.9% / call 50.1% |

Raise sizes are uniform over `[min_raise_to, max_raise_to]` — mean 10,108 across
a 200–20,000 range, which is the midpoint.

## What it is for

Reaching odd states nothing else does. Scripted bots follow narrow paths;
this one produces four-bet wars, tiny raises, short all-ins that do not reopen
action, and every street combination, which is what shakes out engine and
logging bugs.

It is the reference implementation of the randomness contract: seed a small
PRNG from `decision_random` and nothing else, so replaying an identical state
always produces an identical action.

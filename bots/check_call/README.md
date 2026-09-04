# check_call

Never folds, never raises. Calls any bet, checks when checking is free.

## Decision rule

If checking is legal, check. Otherwise call when calling is legal, with fold as
the defensive fallback. It ignores cards, board, history, sizing, and
randomness, so it behaves like a pure calling station on every street.

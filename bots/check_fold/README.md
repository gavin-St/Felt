# check_fold

Checks when it is free, folds to any bet. Never puts a chip in voluntarily.

## Decision rule

If `FELT_LEGAL_CHECK` is present, return `FELT_ACTION_CHECK`; otherwise return
`FELT_ACTION_FOLD`. It ignores cards, board, history, sizing, and randomness.

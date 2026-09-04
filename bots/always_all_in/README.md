# always_all_in

Shoves every hand, from either seat, with any two cards.

## Decision rule

1. If raising is legal, raise to `state->max_raise_to`.
2. If the opponent is already all-in and raising is unavailable, call.
3. Otherwise check when possible, falling back to fold.

It does not inspect its cards, the board, history, or decision randomness. Its
purpose is to exercise the maximum legal raise and all-in call paths.

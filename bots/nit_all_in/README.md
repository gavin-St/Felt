# nit_all_in

Shoves only AA, KK and QQ. Folds everything else.

**18 of 1,326 combinations — 1.4% of hands.**

Deliberately extreme, as the folding end of the shove-or-fold spectrum. It
surrenders the blinds almost every hand and acts only when dealt a premium.

## Decision rule

With AA, KK, or QQ, raise to `state->max_raise_to`, or call if an opponent is
already all-in. With every other hand, check when free and otherwise fold. The
same range is used from both positions and on any decision that remains.

## Worth knowing

This is not a position-aware strategy and does not distinguish opening from
calling. It intentionally applies one extremely tight range everywhere.

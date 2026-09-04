# nit_all_in

Shoves only AA, KK and QQ. Folds everything else.

**18 of 1,326 combinations — 1.4% of hands.**

Deliberately extreme, as the folding end of the shove-or-fold spectrum. It
surrenders the blinds almost every hand and wins only when it is dealt a premium
and paid off, so its own bb/100 shows how expensive folding too much is.

## Measured

Beats `always_all_in` by **+85.1 bb/100** — the smallest margin any bot manages
against a hand that shoves 100%, because folding 98.6% of the time bleeds blinds
faster than the premiums recover them.

## Worth knowing

Its range is very close to the *solved* calling range for facing an all-in at
200 bb, which is AA KK QQ AKs. As a **calling** range for that one spot the
intuition behind this bot is nearly right; it is wrong as an **opening** range,
where the solve is looser at 4.1% because fold equity carries the shove.
See [`../solved_all_in`](../solved_all_in).

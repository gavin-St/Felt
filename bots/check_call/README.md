# check_call

Never folds, never raises. Calls any bet, checks when checking is free.

A station: it pays off every value bet and never wins a pot it did not have the
best hand for. Useful for exercising postflop lines, since it always sees every
street a hand can reach.

## Also a duplicate-play sanity check

Against `always_all_in` it scores **exactly 0.000 bb/100** over 20,000 hands —
raw and equity-adjusted, both zero to the chip.

That is not luck. Neither bot's action depends on its cards, so the two halves
of every duplicate pair are exact mirror images and cancel completely. Any
nonzero result here would mean a bug in the dealing, the duplicate pairing, or
the chip accounting. It is the cheapest end-to-end correctness check in the
repository.

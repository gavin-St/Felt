# check_fold

Checks when it is free, folds to any bet. Never puts a chip in voluntarily.

The simplest possible legal bot, and the floor of the reference set. Its only
job is to be beaten: any bot that cannot beat it is emitting illegal actions or
folding hands it was given for free.

Useful as the first opponent for a new bot, because the result is predictable.
It surrenders the big blind every hand it is dealt one and wins only when the
opponent folds first, so a working opponent should show a large positive
bb/100 against it almost immediately.

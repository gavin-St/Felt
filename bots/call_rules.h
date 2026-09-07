#ifndef FELT_CALL_RULES_H
#define FELT_CALL_RULES_H

#include "felt/bot_api.h"

#include "board_value.h"
#include "range_read.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * When to put in exactly what is owed and no more. Three separate reasons to
 * call, asked in order: the hand is simply ahead of what their line claims;
 * the draw is being offered a price its outs cover; or the hand beats a bluff
 * and the bluff-catch is cheap enough to be worth looking.
 *
 * Folding has no rules of its own. It is what is left when nothing here and
 * nothing in raise_rules.h has said yes.
 */

bool felt_should_call(const FeltGameState* state,
                      const FeltHandValue* value,
                      const FeltRangeRead* read,
                      const FeltDraws* draws);

/* Table-6 call frequency after river, raise, and estimated-bluff adjustments. */
int felt_bluff_catch_frequency(const FeltGameState* state,
                               double delta,
                               const FeltRangeRead* read);

#ifdef __cplusplus
}
#endif

#endif

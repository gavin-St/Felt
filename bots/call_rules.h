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

/* Safety rails for wagers whose price is too small to fold. Any hand calls a
 * wager below 10% of the pot it was made into. Below 20%, a player-made pair
 * or better calls when the wager is all-in or there are no cards to come. */
bool felt_forced_cheap_call(const FeltGameState* state,
                            const FeltHandValue* value);

/*
 * Live outs, in tenths of an out, from the kit's straight and flush counters
 * plus the things they do not count: a pair that can improve, a backdoor, two
 * live overcards. Not the kit's improving_next_cards, which counts every card
 * that changes the hand class and reports fifteen for a bare flush draw.
 */
int felt_live_outs_x10(const FeltGameState* state,
                       const FeltHandValue* value,
                       const FeltDraws* draws);

/* True when the hand has the outs the price is asking for. Never on the
 * river, where there is nothing to come. */
bool felt_draw_is_priced(const FeltGameState* state,
                         const FeltHandValue* value,
                         const FeltDraws* draws);

/* Size-derived bluff-catch frequency after raise and estimated-bluff
 * adjustments. The delta range is split into three equally wide strength
 * buckets calling at 1.5x, 1x, and 0.5x MDF before probability caps and read
 * adjustments. This does not guarantee aggregate MDF defence. The thin band extends to -30
 * against an opening bet in position; out of position and raises of our own
 * bet retain the tighter -25 base. */
int felt_bluff_catch_frequency(const FeltGameState* state,
                               double delta,
                               const FeltRangeRead* read);

#ifdef __cplusplus
}
#endif

#endif

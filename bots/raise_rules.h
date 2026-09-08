#ifndef FELT_RAISE_RULES_H
#define FELT_RAISE_RULES_H

#include "felt/bot_api.h"

#include "bet_sizing.h"
#include "board_value.h"
#include "range_read.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * When to put chips in, and whether the reason is value or a bluff. Nothing
 * here decides how many chips -- that is bet_sizing.c, and keeping the two
 * apart is what lets the same decision take two different sizes.
 *
 * Everything turns on one number: our hand's points on this board, less what
 * the opponent's line claims on the same scale. A rule about a hand class
 * would have to be written twice, once for a weak range and once for a strong
 * one; a rule about the edge is written once.
 */

typedef struct FeltRaisePlan {
  bool raise;
  FeltSizingIntent intent; /* value or thin value; bluffs come separately */
} FeltRaisePlan;

typedef struct FeltBluffOpportunity {
  bool valid;
  /* Pure air is reduced out of position; live draws are not. */
  bool pure_air;
} FeltBluffOpportunity;

/* Value and thin value only. Asked first, before any calling. */
FeltRaisePlan felt_value_raise(const FeltGameState* state,
                               const FeltHandValue* value,
                               const FeltRangeRead* read);

/* First determine whether the hand is a legitimate bluff candidate. Sizing
 * then determines its balanced frequency; eligibility and frequency are kept
 * separate so a large wager cannot inherit a small wager's arbitrary rate. */
FeltBluffOpportunity felt_bluff_opportunity(const FeltGameState* state,
                                             const FeltHandValue* value,
                                             const FeltRangeRead* read,
                                             const FeltDraws* draws);

/* Roll frequency for an eligible bluff candidate. The base share uses the
 * opponent's future call divided by the final called pot, which also works
 * for raises where our risk and their call differ. Explicit realization
 * percentages then distinguish opening bets, raises and re-raises, as well
 * as in-position from out-of-position pure air. */
int felt_balanced_bluff_frequency(const FeltGameState* state,
                                  FeltChips raise_to,
                                  bool pure_air);

#ifdef __cplusplus
}
#endif

#endif

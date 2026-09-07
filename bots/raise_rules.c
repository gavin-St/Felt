#include "raise_rules.h"

#include <stddef.h>

/* Edge thresholds, in points of hand value over what their line claims. */
#define EDGE_RAISE 20          /* enough to raise a bet with */
#define EDGE_BET_FOR_STACKS 22 /* enough to start building a pot */
#define EDGE_THIN_VALUE 6      /* ahead, but not by enough to build one */

/*
 * The edge cannot tell holding a hand from neither player holding anything,
 * and those want opposite actions. Anything below this is air, whatever the
 * edge says, and air is the only thing allowed to bluff.
 */
#define SHOWDOWN_VALUE_POINTS 14

FeltRaisePlan felt_value_raise(const FeltGameState* state,
                               const FeltHandValue* value,
                               const FeltRangeRead* read) {
  FeltRaisePlan plan = {0};
  if (state == NULL || value == NULL || read == NULL || !value->valid) {
    return plan;
  }
  const int edge = value->points - read->score;

  if (state->to_call > 0) {
    if (edge >= EDGE_RAISE) {
      plan.raise = true;
      plan.intent = FELT_SIZING_VALUE;
    }
    return plan;
  }
  if (edge >= EDGE_BET_FOR_STACKS) {
    plan.raise = true;
    plan.intent = FELT_SIZING_VALUE;
  } else if (edge >= EDGE_THIN_VALUE) {
    plan.raise = true;
    plan.intent = FELT_SIZING_THIN_VALUE;
  }
  return plan;
}

bool felt_bluff_raise(const FeltGameState* state,
                      const FeltHandValue* value,
                      const FeltRangeRead* read) {
  if (state == NULL || value == NULL || read == NULL || !value->valid) {
    return false;
  }
  if (value->points >= SHOWDOWN_VALUE_POINTS) {
    return false; /* a hand worth showing down is not a bluffing candidate */
  }
  const uint64_t roll = state->decision_random >> 24U;
  if (read->score < 35) return roll % UINT64_C(3) == 0U;
  if (read->score < 55) return roll % UINT64_C(5) == 0U;
  return roll % UINT64_C(8) == 0U;
}

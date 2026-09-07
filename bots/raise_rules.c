#include "raise_rules.h"

#include <stddef.h>

#define DELTA_BET_VALUE 22
#define DELTA_BET_THIN 6
#define DELTA_BLUFF_MAX (-10)
#define DELTA_RAISE_MERGED 20
#define DELTA_RAISE_POLARISED 28
#define DELTA_CALL 6
#define DELTA_THIN_CATCH (-25)

static bool facing_raise(const FeltGameState* state) {
  return state->my_street_contribution > 0 && state->to_call > 0;
}

static bool percent_roll(const FeltGameState* state,
                         unsigned shift,
                         int percent) {
  if (percent <= 0) return false;
  if (percent >= 100) return true;
  return (int)((state->decision_random >> shift) % UINT64_C(100)) < percent;
}

FeltRaisePlan felt_value_raise(const FeltGameState* state,
                               const FeltHandValue* value,
                               const FeltRangeRead* read) {
  FeltRaisePlan plan = {0};
  if (state == NULL || value == NULL || read == NULL || !value->valid) {
    return plan;
  }
  const double delta = felt_range_delta(value, read);
  const bool polarised = read->polarisation >= FELT_POLARISED_AT;

  if (state->to_call > 0) {
    int threshold =
        polarised ? DELTA_RAISE_POLARISED : DELTA_RAISE_MERGED;
    if (facing_raise(state)) threshold += 10;
    if (delta >= (double)threshold) {
      plan.raise = true;
      plan.intent = FELT_SIZING_VALUE;
    }
    return plan;
  }
  if (delta >= DELTA_BET_VALUE) {
    plan.raise = true;
    plan.intent = FELT_SIZING_VALUE;
  } else if (delta >= DELTA_BET_THIN &&
             percent_roll(state, 0U, polarised ? 40 : 75)) {
    plan.raise = true;
    plan.intent = FELT_SIZING_THIN_VALUE;
  }
  return plan;
}

bool felt_bluff_raise(const FeltGameState* state,
                      const FeltHandValue* value,
                      const FeltRangeRead* read,
                      const FeltDraws* draws) {
  if (state == NULL || value == NULL || read == NULL || !value->valid) {
    return false;
  }
  const bool polarised = read->polarisation >= FELT_POLARISED_AT;
  const bool live_eight = draws != NULL && draws->valid &&
                          draws->improving_next_cards >= 8U;
  const bool gutshot = draws != NULL && draws->valid &&
                       (draws->flags & FELT_DRAW_GUTSHOT) != 0U;
  const bool any_draw = draws != NULL && draws->valid &&
                        draws->improving_next_cards > 0U;
  const bool raised = facing_raise(state);
  const double delta = felt_range_delta(value, read);

  if (state->to_call <= 0) {
    if (live_eight) {
      return percent_roll(state, 16U, polarised ? 25 : 50);
    }
    if (gutshot) {
      return percent_roll(state, 16U, polarised ? 10 : 25);
    }
    if (!any_draw && delta < DELTA_BLUFF_MAX) {
      int rate = polarised ? 15 : 30;
      if (read->score <= 40) rate += 10;
      return percent_roll(state, 16U, rate);
    }
    return false;
  }

  const int threshold_shift = raised ? 10 : 0;
  if (live_eight && delta < (double)(DELTA_CALL + threshold_shift)) {
    return percent_roll(state, 16U, polarised ? 10 : 30);
  }
  if (!any_draw &&
      delta < (double)(DELTA_THIN_CATCH + threshold_shift) &&
      read->score <= 50) {
    if (raised) {
      /* Once our bet has been raised, a merged range still gets a small
       * bluff re-raise; a polarised one gets none. */
      return percent_roll(state, 16U, polarised ? 0 : 10);
    }
    return percent_roll(state, 16U, polarised ? 5 : 20);
  }
  return false;
}

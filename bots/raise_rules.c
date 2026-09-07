#include "raise_rules.h"

#include "call_rules.h"

#include <stddef.h>

#define DELTA_BET_VALUE 22
#define DELTA_BET_THIN 6
#define DELTA_BLUFF_MAX (-10)
#define DELTA_RAISE_MERGED 20
#define DELTA_RAISE_POLARISED 28
#define DELTA_CALL 6
#define DELTA_THIN_CATCH (-25)
/* Facing a raise of our own bet, every threshold moves up by this much. */
#define RAISE_SHIFT 8

/*
 * Betting again is a stronger claim than betting once. Nothing in the read
 * knows we have already bet -- it scores their line, not ours -- so the same
 * hand kept producing the same thin bet on every street, and a medium pair
 * could barrel three times for no reason other than that the number had not
 * moved. The bar rises with each barrel instead.
 */
#define BARREL_SHIFT_SECOND 6
#define BARREL_SHIFT_THIRD 12

/* Outs, in tenths, that separate a semi-bluff worth making from a bare one. */
#define OUTS_STRONG_X10 80
#define OUTS_WEAK_X10 30

static bool facing_raise(const FeltGameState* state) {
  return state->my_street_contribution > 0 && state->to_call > 0;
}

static int barrel_shift(const FeltGameState* state) {
  const uint32_t barrels = felt_own_barrels(state);
  if (barrels == 0U) return 0;
  return barrels == 1U ? BARREL_SHIFT_SECOND : BARREL_SHIFT_THIRD;
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
    int threshold = polarised ? DELTA_RAISE_POLARISED : DELTA_RAISE_MERGED;
    if (facing_raise(state)) threshold += RAISE_SHIFT;
    if (delta >= (double)threshold) {
      plan.raise = true;
      plan.intent = FELT_SIZING_VALUE;
    }
    return plan;
  }

  const int shift = barrel_shift(state);
  if (delta >= (double)(DELTA_BET_VALUE + shift)) {
    plan.raise = true;
    plan.intent = FELT_SIZING_VALUE;
  } else if (delta >= (double)(DELTA_BET_THIN + shift) &&
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
  const int outs = felt_live_outs_x10(state, value, draws);
  const bool strong_draw = outs >= OUTS_STRONG_X10;
  const bool weak_draw = outs >= OUTS_WEAK_X10 && outs < OUTS_STRONG_X10;
  const bool raised = facing_raise(state);
  const double delta = felt_range_delta(value, read);

  if (state->to_call <= 0) {
    if (strong_draw) {
      return percent_roll(state, 16U, polarised ? 25 : 50);
    }
    if (weak_draw) {
      return percent_roll(state, 16U, polarised ? 10 : 25);
    }
    if (outs <= 0 && delta < DELTA_BLUFF_MAX) {
      int rate = polarised ? 15 : 30;
      if (read->score <= 40) rate += 10;
      return percent_roll(state, 16U, rate);
    }
    return false;
  }

  const int threshold_shift = raised ? RAISE_SHIFT : 0;
  if (strong_draw && delta < (double)(DELTA_CALL + threshold_shift)) {
    return percent_roll(state, 16U, polarised ? 10 : 30);
  }
  if (weak_draw && delta < (double)(DELTA_CALL + threshold_shift)) {
    /* A weak draw raises rarely; the rest of the time the call rules price
     * it, and failing that the bluff-catch table has the last word. */
    return percent_roll(state, 16U, polarised ? 5 : 15);
  }
  if (outs <= 0 && delta < (double)(DELTA_THIN_CATCH + threshold_shift) &&
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

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

/*
 * How much has to be behind, against the pot, for a raise with nothing to be
 * a bluff rather than a shove. Pure air needs the whole rest of the hand as a
 * threat; a weak draw needs less, because it still has cards to come; a
 * strong draw needs none, since getting it in with equity is fine.
 */
#define BLUFF_MIN_STACK_POT_PERCENT 150
#define WEAK_DRAW_MIN_STACK_POT_PERCENT 75

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
             percent_roll(state, 0U,
                          (read->hero_out_of_position
                               ? (polarised ? 25 : 50)
                               : (polarised ? 40 : 75)))) {
    plan.raise = true;
    plan.intent = FELT_SIZING_THIN_VALUE;
  }
  return plan;
}

FeltBluffOpportunity felt_bluff_opportunity(const FeltGameState* state,
                                             const FeltHandValue* value,
                                             const FeltRangeRead* read,
                                             const FeltDraws* draws) {
  FeltBluffOpportunity opportunity = {0};
  if (state == NULL || value == NULL || read == NULL || !value->valid) {
    return opportunity;
  }
  const bool polarised = read->polarisation >= FELT_POLARISED_AT;
  const int outs = felt_live_outs_x10(state, value, draws);
  const bool strong_draw = outs >= OUTS_STRONG_X10;
  const bool weak_draw = outs >= OUTS_WEAK_X10 && outs < OUTS_STRONG_X10;
  const bool raised = facing_raise(state);
  const double delta = felt_range_delta(value, read);

  if (state->to_call <= 0) {
    if (strong_draw) {
      opportunity.valid = true;
      return opportunity;
    }
    if (weak_draw &&
        felt_stack_pot_percent(state) >= WEAK_DRAW_MIN_STACK_POT_PERCENT) {
      opportunity.valid = true;
      return opportunity;
    }
    /* A bet with nothing wants the rest of the hand behind it too. Shallow
     * enough and the bet is a shove by another name. */
    if (outs <= 0 && delta < DELTA_BLUFF_MAX &&
        felt_stack_pot_percent(state) >= BLUFF_MIN_STACK_POT_PERCENT) {
      opportunity.valid = true;
      opportunity.pure_air = true;
    }
    return opportunity;
  }

  const int threshold_shift = raised ? RAISE_SHIFT : 0;
  if (strong_draw && delta < (double)(DELTA_CALL + threshold_shift)) {
    opportunity.valid = true;
    return opportunity;
  }
  if (weak_draw && delta < (double)(DELTA_CALL + threshold_shift) &&
      felt_stack_pot_percent(state) >= WEAK_DRAW_MIN_STACK_POT_PERCENT) {
    opportunity.valid = true;
    return opportunity;
  }
  if (outs <= 0 && delta < (double)(DELTA_THIN_CATCH + threshold_shift) &&
      read->score <= 50 &&
      felt_stack_pot_percent(state) >= BLUFF_MIN_STACK_POT_PERCENT) {
    if (raised) {
      /* Once our bet has been raised, a merged range still gets a small
       * bluff re-raise; a polarised one gets none. */
      if (polarised) return opportunity;
    }
    opportunity.valid = true;
    opportunity.pure_air = true;
  }
  return opportunity;
}

int felt_balanced_bluff_frequency(const FeltGameState* state,
                                  FeltChips raise_to,
                                  bool pure_air) {
  if (state == NULL || raise_to <= state->my_street_contribution ||
      state->pot <= 0) {
    return 0;
  }
  const long double risk =
      (long double)(raise_to - state->my_street_contribution);
  const long double denominator = (long double)state->pot + 2.0L * risk;
  int frequency = denominator > 0.0L
                      ? (int)(100.0L * risk / denominator + 0.5L)
                      : 0;
  if (pure_air && state->street != FELT_STREET_PREFLOP &&
      state->position == FELT_POSITION_BIG_BLIND) {
    frequency /= 2;
  }
  if (frequency < 0) frequency = 0;
  if (frequency > 50) frequency = 50;
  return frequency;
}

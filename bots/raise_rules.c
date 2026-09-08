#include "raise_rules.h"

#include "call_rules.h"

#include <stddef.h>

/*
 * Delta is a hand's points less half the opponent's range claim less fifty.
 * The fifty is the midpoint of the label scale, not the middle of the deck:
 * measured over 200,000 random postflop spots the median holding scores 16,
 * and a score of 50 is the 84th percentile. Top pair averages 39.7, so it
 * arrives at delta -10 -- ten points below "average" -- and under the old
 * thresholds of 22 and 6 it could never be bet for value at all. These are
 * set from the score distribution instead: top pair and better is value,
 * middle pair and better is thin value, and anything below that has no
 * showdown value worth protecting and is a bluff candidate. The bluff ceiling
 * and the thin floor are the same number so the two bands abut with no gap,
 * the same way the bluff-catching bands do.
 */
#define DELTA_BET_VALUE (-10)
#define DELTA_BET_THIN (-24)
#define DELTA_BLUFF_MAX (-24)
#define DELTA_RAISE_MERGED 20
#define DELTA_RAISE_POLARISED 28
#define OOP_RAISE_SHIFT 8
#define DELTA_CALL 6
#define DELTA_THIN_CATCH (-25)
#define INITIAL_BET_THIN_CATCH (-30)
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

/* Portions of the sizing-derived base share realized by each eligible
 * candidate group. These are explicit because an opening bet, a raise and a
 * re-raise risk very different amounts and represent successively stronger
 * ranges. */
#define SEMI_BLUFF_REALIZATION_PERCENT 50
#define IP_OPEN_AIR_REALIZATION_PERCENT 100
#define OOP_OPEN_AIR_REALIZATION_PERCENT 25
#define IP_RAISE_AIR_REALIZATION_PERCENT 50
#define OOP_RAISE_AIR_REALIZATION_PERCENT 15
#define RERAISE_AIR_REALIZATION_PERCENT 5
/* Missed draws cannot provide semi-bluffs on the river, so pure air supplies
 * more of the bluff side of the range. */
#define RIVER_AIR_REALIZATION_PERCENT 125

static bool facing_raise(const FeltGameState* state) {
  return state->my_street_contribution > 0 && state->to_call > 0;
}

/*
 * Position is worth something because there are streets left to be outplayed
 * on and equity left to realise. On the river there are neither: the money
 * goes in once more and the hands are shown. So every discount for acting
 * first is dropped there, and the river is played the same way from both
 * seats.
 */
static bool positional_discount(const FeltGameState* state) {
  return state->street != FELT_STREET_RIVER;
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
    if (read->hero_out_of_position && positional_discount(state)) {
      threshold += OOP_RAISE_SHIFT;
    }
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
                          (read->hero_out_of_position &&
                                   positional_discount(state)
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
  const int thin_floor =
      raised ? DELTA_THIN_CATCH + threshold_shift
             : (state->position == FELT_POSITION_BIG_BLIND
                    ? DELTA_THIN_CATCH
                    : INITIAL_BET_THIN_CATCH);
  if (strong_draw && delta < (double)(DELTA_CALL + threshold_shift)) {
    opportunity.valid = true;
    return opportunity;
  }
  /* Weak draws do not raise a bet. If the price is right, call_rules keeps
   * them; otherwise they fold. Only eight-out-plus draws semi-bluff raise. */
  if (outs <= 0 && delta < (double)thin_floor &&
      (raised || read->score <= 50) &&
      felt_stack_pot_percent(state) >= BLUFF_MIN_STACK_POT_PERCENT) {
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
  /* For an opening bet, our risk and their future call are equal, reducing
   * to risk / (pot + 2*risk). For a raise they are not equal: our risk also
   * contains the chips needed to call their bet. The balanced bluff share is
   * what they must call divided by the final pot they would create. */
  const long double opponent_call =
      state->to_call > 0
          ? (long double)(raise_to - state->opp_street_contribution)
          : risk;
  const long double denominator =
      (long double)state->pot + risk + opponent_call;
  int frequency = denominator > 0.0L && opponent_call > 0.0L
                      ? (int)(100.0L * opponent_call / denominator + 0.5L)
                      : 0;
  int realization = SEMI_BLUFF_REALIZATION_PERCENT;
  if (pure_air) {
    if (facing_raise(state)) {
      realization = RERAISE_AIR_REALIZATION_PERCENT;
    } else if (state->to_call > 0) {
      realization = (state->position == FELT_POSITION_BUTTON ||
                     !positional_discount(state))
                        ? IP_RAISE_AIR_REALIZATION_PERCENT
                        : OOP_RAISE_AIR_REALIZATION_PERCENT;
    } else {
      realization = (state->position == FELT_POSITION_BUTTON ||
                     !positional_discount(state))
                        ? IP_OPEN_AIR_REALIZATION_PERCENT
                        : OOP_OPEN_AIR_REALIZATION_PERCENT;
    }
    if (state->street == FELT_STREET_RIVER) {
      realization =
          (realization * RIVER_AIR_REALIZATION_PERCENT + 50) / 100;
      if (realization > 100) realization = 100;
    }
  }
  frequency = frequency * realization / 100;
  if (frequency < 0) frequency = 0;
  if (frequency > 50) frequency = 50;
  return frequency;
}

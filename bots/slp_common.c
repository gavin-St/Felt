#include "slp_common.h"

#include "felt/bot_kit.h"

#include <stdbool.h>

static FeltAction aggressive_action(const FeltGameState* state) {
  if (state->to_call > 0) {
    return felt_raise_to_multiple(state, 3U);
  }
  return felt_raise_to_pot_fraction(state, 0.75);
}

static bool is_small_pair(const FeltMadeHand* made) {
  return made->valid && made->category == FELT_MADE_ONE_PAIR &&
         made->pair_relation != FELT_PAIR_NONE &&
         !felt_is_top_pair_or_better(made);
}

/* Street-local test for facing a raise rather than an opening bet: chips
 * already committed on this street plus more still owed means the opponent
 * raised us. No history is read, so the policy stays street-local. */
static bool facing_raise(const FeltGameState* state) {
  return state->my_street_contribution > 0 && state->to_call > 0;
}

static bool is_overpair_or_better(const FeltMadeHand* made) {
  return made->valid &&
         (made->category >= FELT_MADE_TWO_PAIR ||
          (made->category == FELT_MADE_ONE_PAIR &&
           made->pair_relation == FELT_PAIR_OVERPAIR));
}

FeltAction slp_act(const FeltGameState* state, SlpProfile profile) {
  if (state == NULL) {
    return felt_check_or_fold(state);
  }
  if (state->street == FELT_STREET_PREFLOP) {
    return felt_preflop_baseline_action(state);
  }

  const FeltMadeHand made =
      felt_made_hand(state->hole, state->board, state->board_count);
  const FeltDraws draws =
      felt_draws(state->hole, state->board, state->board_count);
  if (!made.valid || !draws.valid) {
    return felt_check_or_fold(state);
  }

  if (profile == SLP_EXPLOIT_FOLD && state->to_call > 0) {
    return is_overpair_or_better(&made) ? aggressive_action(state)
                                        : felt_check_or_fold(state);
  }
  if (felt_is_top_pair_or_better(&made)) {
    /* A raise of our own bet is a far stronger range than an opening bet, so
     * one pair no longer continues against it -- only two pair or better. */
    if (profile == SLP_BALANCE && facing_raise(state) &&
        made.category == FELT_MADE_ONE_PAIR) {
      return felt_check_or_fold(state);
    }
    if (profile == SLP_BALANCE && state->to_call > 0 &&
        made.category == FELT_MADE_ONE_PAIR) {
      return felt_call_or_check(state);
    }
    if (profile == SLP_BALANCE && state->decision_random % UINT64_C(3) == 0U) {
      return felt_call_or_check(state);
    }
    return aggressive_action(state);
  }
  if (is_small_pair(&made) || draws.flags != FELT_DRAW_NONE) {
    /* Against an opening bet these always continue: folding them to a bet
     * larger than the prior pot used to cost roughly 8 bb/hand against a
     * bluff-heavy opponent. Against a raise, a third of the draws carry on --
     * enough to stay unpredictable without paying off a nutted range -- and
     * small pairs give up. */
    if (profile == SLP_BALANCE && facing_raise(state)) {
      const bool has_draw = draws.flags != FELT_DRAW_NONE;
      if (has_draw && (state->decision_random >> 16U) % UINT64_C(3) == 0U) {
        return felt_call_or_check(state);
      }
      return felt_check_or_fold(state);
    }
    return felt_call_or_check(state);
  }

  if (profile == SLP_BALANCE && state->to_call > 0) {
    return felt_check_or_fold(state);
  }
  if (profile == SLP_BLUFF ||
      profile == SLP_EXPLOIT_FOLD ||
      (profile == SLP_BALANCE &&
       (state->decision_random & UINT64_C(1)) != 0U)) {
    return aggressive_action(state);
  }
  return felt_check_or_fold(state);
}

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
    return aggressive_action(state);
  }
  if (is_small_pair(&made) || draws.flags != FELT_DRAW_NONE) {
    return felt_call_or_check(state);
  }

  if (profile == SLP_BLUFF ||
      profile == SLP_EXPLOIT_FOLD ||
      (profile == SLP_BALANCE &&
       (state->decision_random & UINT64_C(1)) != 0U)) {
    return aggressive_action(state);
  }
  return felt_check_or_fold(state);
}

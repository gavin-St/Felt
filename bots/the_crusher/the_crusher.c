/*
 * the-crusher -- the first bot that asks what the opponent has.
 *
 * The target is not the maximum against any one opponent. It is to have no
 * matchup it loses, which is a different and harder thing: every rule here
 * trades a little of the maximum against the weak bots for not being
 * exploitable by the strong ones.
 *
 * Four questions, in order, and nothing else:
 *
 *   1. What is our hand worth on this board?      board_value.c
 *   2. What does their line claim, on that scale? range_read.c
 *   3. Is that enough to raise, or to call?       raise_rules.c, call_rules.c
 *   4. If we are putting chips in, how many?      bet_sizing.c
 *
 * Folding has no rules. It is what is left when nothing else said yes.
 *
 * Splitting the fourth question out from the third is the whole reason the
 * bot can take two different sizes with the same holding: by the time sizing
 * runs, the decision has already been made and only the number is open.
 */

#include "../bet_sizing.h"
#include "../board_value.h"
#include "../call_rules.h"
#include "../raise_rules.h"
#include "../range_read.h"

#include "felt/bot_kit.h"

#include <stdbool.h>

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char* felt_bot_name(void) {
  felt_bot_kit_warmup();
  return "the-crusher";
}

static FeltChips effective_stack(const FeltGameState* state) {
  return state->my_stack < state->opp_stack ? state->my_stack
                                            : state->opp_stack;
}

static bool facing_raise(const FeltGameState* state) {
  return state->my_street_contribution > 0 && state->to_call > 0;
}

static FeltAction sized_action(const FeltGameState* state,
                               const FeltRangeRead* read,
                               const FeltBoardTexture* texture,
                               const FeltDraws* draws,
                               FeltSizingIntent intent) {
  const int geometric = felt_geometric_bet_percent(
      state->pot, effective_stack(state), state->street);
  const FeltSizing sizing =
      felt_choose_size(state, read, texture, draws, intent,
                       facing_raise(state), geometric);
  return felt_raise_to_pot_fraction(state, sizing.fraction);
}

FeltAction felt_bot_act(const FeltGameState* state) {
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
  const FeltBoardTexture texture =
      felt_board_texture(state->board, state->board_count);
  if (!made.valid || !draws.valid || !texture.valid) {
    return felt_check_or_fold(state);
  }

  const FeltHandValue value = felt_board_relative_value(&made, &texture);
  const FeltRangeRead read = felt_read_range(state, &texture);
  if (!value.valid || !read.valid) {
    return felt_check_or_fold(state);
  }

  const FeltRaisePlan plan = felt_value_raise(state, &value, &read);
  if (plan.raise) {
    return sized_action(state, &read, &texture, &draws, plan.intent);
  }
  if (felt_should_call(state, &value, &read, &draws)) {
    return felt_call_or_check(state);
  }
  if (felt_bluff_raise(state, &value, &read)) {
    return sized_action(state, &read, &texture, &draws, FELT_SIZING_BLUFF);
  }
  return felt_check_or_fold(state);
}

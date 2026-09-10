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

static FeltAction raise_to_opponent_multiple(const FeltGameState* state,
                                             double multiple) {
  if ((state->legal_actions & FELT_LEGAL_RAISE_TO) == 0U ||
      state->opp_street_contribution <= 0) {
    return felt_call_or_check(state);
  }
  FeltChips target =
      (FeltChips)((double)state->opp_street_contribution * multiple + 0.5);
  if (target < state->min_raise_to) target = state->min_raise_to;
  if (target > state->max_raise_to) target = state->max_raise_to;
  FeltAction action = {0};
  action.type = FELT_ACTION_RAISE_TO;
  action.amount_to = target;
  return action;
}

static FeltSizing choose_sizing(const FeltGameState* state,
                                const FeltRangeRead* read,
                                const FeltBoardTexture* texture,
                                const FeltDraws* draws,
                                FeltSizingIntent intent) {
  const int geometric = felt_geometric_bet_percent(
      state->pot, effective_stack(state), state->street);
  return felt_choose_size(state, read, texture, draws, intent,
                          facing_raise(state), geometric);
}

static FeltAction action_for_sizing(const FeltGameState* state,
                                    FeltSizing sizing) {
  if (sizing.relative_to_opponent) {
    return raise_to_opponent_multiple(state, sizing.fraction);
  }
  return felt_raise_to_pot_fraction(state, sizing.fraction);
}

static FeltAction sized_action(const FeltGameState* state,
                               const FeltRangeRead* read,
                               const FeltBoardTexture* texture,
                               const FeltDraws* draws,
                               FeltSizingIntent intent) {
  return action_for_sizing(
      state, choose_sizing(state, read, texture, draws, intent));
}

static bool try_bluff(const FeltGameState* state,
                      const FeltRangeRead* read,
                      const FeltBoardTexture* texture,
                      const FeltDraws* draws,
                      FeltBluffOpportunity opportunity,
                      FeltAction* action) {
  if (!opportunity.valid || action == NULL) return false;
  const FeltSizing sizing =
      choose_sizing(state, read, texture, draws, FELT_SIZING_BLUFF);
  const FeltAction candidate = action_for_sizing(state, sizing);
  if (candidate.type != FELT_ACTION_RAISE_TO) return false;

  const int frequency = felt_balanced_bluff_frequency(
      state, candidate.amount_to, opportunity.pure_air);
  if ((int)((state->decision_random >> 16U) % UINT64_C(100)) >= frequency) {
    return false;
  }
  *action = candidate;
  /* Say so, rather than leaving the ledger to infer it from the cards. A
   * semi-bluff with a big draw is still a bluff here: the bet is not being
   * made because the hand is ahead. */
  action->flags |= FELT_ACTION_FLAG_BLUFF;
  return true;
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

  const FeltHandValue value =
      felt_board_relative_value(state, &made, &draws, &texture);
  const FeltRangeRead read = felt_read_range(state, &texture);
  if (!value.valid || !read.valid) {
    return felt_check_or_fold(state);
  }

  /* A shared straight or better is a chop-or-lose decision, not private
   * air. Enumerate what can beat the board, removing our blockers. The
   * baseline treats remaining combinations equally; it is not a learned
   * betting range. A tie returns half the pot after calling. */
  if (state->street == FELT_STREET_RIVER && made.plays_board &&
      made.category >= FELT_MADE_STRAIGHT) {
    if (state->to_call <= 0) return felt_call_or_check(state);
    const int chop_share = felt_board_chop_share_basis_points(state->hole, state->board);
    if (chop_share >= 0) {
      return (int64_t)chop_share * (state->pot + state->to_call) >=
                     INT64_C(20000) * state->to_call
                 ? felt_call_or_check(state) : felt_check_or_fold(state);
    }
  }

  const FeltRaisePlan plan = felt_value_raise(state, &value, &read);
  if (plan.raise) {
    return sized_action(state, &read, &texture, &draws, plan.intent);
  }

  const FeltBluffOpportunity bluff =
      felt_bluff_opportunity(state, &value, &read, &draws);
  FeltAction bluff_action = {0};
  /* A legitimate semi-bluff raise keeps priority over calling. A pure-air
   * bluff does not turn a mandatory cheap call into a much larger wager. */
  if (!bluff.pure_air &&
      try_bluff(state, &read, &texture, &draws, bluff, &bluff_action)) {
    return bluff_action;
  }
  if (felt_forced_cheap_call(state, &value)) {
    return felt_call_or_check(state);
  }
  if (bluff.pure_air &&
      try_bluff(state, &read, &texture, &draws, bluff, &bluff_action)) {
    return bluff_action;
  }
  if (felt_should_call(state, &value, &read, &draws)) {
    return felt_call_or_check(state);
  }
  return felt_check_or_fold(state);
}

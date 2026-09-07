/*
 * the-crusher -- the first bot that asks what the opponent has.
 *
 * Everything before it scores its own hand and stops. This one scores the
 * opponent's line on the same scale and subtracts:
 *
 *     edge = what our hand is worth on this board
 *          - what their line claims to be worth on this board
 *
 * That single number does most of the work. Against a range that has only
 * checked, the edge is large, so the same hand value bets thinner and calls
 * wider; against a range that has raised twice, the edge collapses, so the
 * same hand stops raising and starts folding. Neither behaviour has a rule of
 * its own -- both fall out of the subtraction.
 *
 * The second half is sizing. A fixed fraction of the pot cannot get two
 * hundred big blinds in by the river, so when the edge is big enough to want
 * stacks the bet is the geometric one: the fraction that, repeated on every
 * street left, ends exactly all in.
 *
 * Bluff frequency is the one thing read directly off the opponent's score,
 * because it does not follow from the edge: weak ranges get bluffed at more
 * often than strong ones. Polarisation is deliberately left out.
 */

#include "../board_value.h"
#include "../range_read.h"

#include "felt/bot_kit.h"

#include <stdbool.h>

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char* felt_bot_name(void) {
  felt_bot_kit_warmup();
  return "the-crusher";
}

/* Edge thresholds, in points. */
#define EDGE_RAISE 20
#define EDGE_CALL 6
#define EDGE_BLUFF_CATCH (-14)
#define EDGE_BET_FOR_STACKS 22
#define EDGE_THIN_VALUE 6

/*
 * The edge on its own cannot tell "I hold a hand" from "neither of us holds
 * anything", and the two want opposite actions: the first checks it down, the
 * second bluffs. So both the checking band and the bluff-catching band ask for
 * a hand that could actually win a showdown, and everything below this falls
 * through to the bluff.
 */
#define SHOWDOWN_VALUE_POINTS 14

/* A bluff-catch is worth it up to this share of the pot the call would build.
 * The weaker their range, the more it is worth paying to look. */
static int bluff_catch_ceiling(int range_score) {
  int ceiling = 42 - range_score / 3;
  if (ceiling < 12) ceiling = 12;
  if (ceiling > 45) ceiling = 45;
  return ceiling;
}

/* One in three against a range that has shown nothing, one in eight against
 * one that has shown a lot. */
static bool bluff_now(const FeltGameState* state, int range_score) {
  const uint64_t roll = state->decision_random >> 24U;
  if (range_score < 35) return roll % UINT64_C(3) == 0U;
  if (range_score < 55) return roll % UINT64_C(5) == 0U;
  return roll % UINT64_C(8) == 0U;
}

static FeltChips effective_stack(const FeltGameState* state) {
  return state->my_stack < state->opp_stack ? state->my_stack
                                            : state->opp_stack;
}

/* The size that gets the stacks in over the streets that are left. */
static double stacking_fraction(const FeltGameState* state) {
  const int percent = felt_geometric_bet_percent(
      state->pot, effective_stack(state), state->street);
  if (percent <= 0) {
    return 0.75;
  }
  return (double)percent / 100.0;
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

  const int edge = value.points - read.score;
  const bool has_draw = draws.improving_next_cards > 0;

  if (state->to_call > 0) {
    if (edge >= EDGE_RAISE) {
      return felt_raise_to_pot_fraction(state, stacking_fraction(state));
    }
    if (edge >= EDGE_CALL) {
      return felt_call_or_check(state);
    }
    /* A draw is priced on its own arithmetic, whoever we are up against. */
    if (has_draw && felt_draw_price_is_right(state, &draws)) {
      return felt_call_or_check(state);
    }
    if (value.points >= SHOWDOWN_VALUE_POINTS && edge >= EDGE_BLUFF_CATCH &&
        felt_call_price_percent(state) <= bluff_catch_ceiling(read.score)) {
      return felt_call_or_check(state);
    }
    if (bluff_now(state, read.score)) {
      return felt_raise_to_multiple(state, 3U);
    }
    return felt_check_or_fold(state);
  }

  if (edge >= EDGE_BET_FOR_STACKS) {
    return felt_raise_to_pot_fraction(state, stacking_fraction(state));
  }
  if (edge >= EDGE_THIN_VALUE) {
    /* Ahead, but not by enough to build a pot with: take a third. */
    return felt_raise_to_pot_fraction(state, 0.33);
  }
  if (value.points >= SHOWDOWN_VALUE_POINTS) {
    /* Worth showing down, not worth building a pot with. */
    return felt_call_or_check(state);
  }
  if (bluff_now(state, read.score)) {
    return felt_raise_to_pot_fraction(state, 0.5);
  }
  return felt_check_or_fold(state);
}

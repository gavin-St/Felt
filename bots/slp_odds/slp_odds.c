/*
 * slp-odds -- slp-balance, plus the two sums it never did.
 *
 * Preflop is the same baseline chart. After the flop, two things change and
 * nothing else does:
 *
 *   1. A hand is scored against the board it is sitting on rather than by its
 *      own class, and the score picks the action. Top pair is a strong hand on
 *      a rainbow board and a marginal one when four to a flush is out.
 *   2. A draw continues on price. Outs are counted, turned into equity at two
 *      percent a card, and compared with what the call costs as a share of the
 *      pot it would create.
 *
 * It also knows the difference between an opening bet and a raise of its own
 * bet -- a far stronger range -- and asks the two value bands for more points
 * when facing one. Everything it reads is on the current street, so this is
 * still a street-local policy.
 */

#include "../board_value.h"

#include "felt/bot_kit.h"

#include <stdbool.h>

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char* felt_bot_name(void) {
  felt_bot_kit_warmup();
  return "slp-odds";
}

/* A made hand worth calling for is worth calling only at a sane price. These
 * are the ceilings, as a share of the pot the call would build. */
#define MEDIUM_MAX_PRICE_PERCENT 35
#define MARGINAL_MAX_PRICE_PERCENT 18

/* Chips in already on this street, and more owed, means we were raised. */
static bool facing_raise(const FeltGameState* state) {
  return state->my_street_contribution > 0 && state->to_call > 0;
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
  if (!value.valid) {
    return felt_check_or_fold(state);
  }
  const bool has_draw = draws.improving_next_cards > 0;
  const bool raised = facing_raise(state);
  const FeltHandBand band = felt_band_for_points(value.points, raised);

  if (state->to_call > 0) {
    const int price = felt_call_price_percent(state);
    switch (band) {
      case FELT_BAND_NUTTED:
        /* One re-raise in three is flatted instead, so the strongest hands do
         * not all arrive the same way. */
        if (raised && (state->decision_random >> 8U) % UINT64_C(3) == 0U) {
          return felt_call_or_check(state);
        }
        return felt_raise_to_multiple(state, 3U);
      case FELT_BAND_STRONG:
        return felt_call_or_check(state);
      case FELT_BAND_MEDIUM:
        if (has_draw && felt_draw_price_is_right(state, &draws)) {
          return felt_call_or_check(state);
        }
        return price <= MEDIUM_MAX_PRICE_PERCENT
                   ? felt_call_or_check(state)
                   : felt_check_or_fold(state);
      case FELT_BAND_MARGINAL:
        if (has_draw && felt_draw_price_is_right(state, &draws)) {
          return felt_call_or_check(state);
        }
        return price <= MARGINAL_MAX_PRICE_PERCENT
                   ? felt_call_or_check(state)
                   : felt_check_or_fold(state);
      case FELT_BAND_AIR:
      default:
        /* One bet in five is raised with nothing made, and a re-raise gets
         * the same treatment, so neither the raising range nor the re-raising
         * range is only ever value. With a draw this is a semi-bluff, with
         * nothing it is a pure one. Reaching the second case at all means we
         * bluffed once already and were raised, and each decision draws its
         * own randomness, so the two compound rather than repeat: about one
         * air three-bet for every twenty-five air bets. */
        if ((state->decision_random >> 24U) % UINT64_C(5) == 0U) {
          return felt_raise_to_multiple(state, 3U);
        }
        if (has_draw && felt_draw_price_is_right(state, &draws)) {
          return felt_call_or_check(state);
        }
        return felt_check_or_fold(state);
    }
  }

  /* Nobody has bet. */
  switch (band) {
    case FELT_BAND_NUTTED:
      /* The same one-in-three trap slp-balance uses on its biggest hands. */
      if (state->decision_random % UINT64_C(3) == 0U) {
        return felt_call_or_check(state);
      }
      return felt_raise_to_pot_fraction(state, 0.75);
    case FELT_BAND_STRONG:
      return felt_raise_to_pot_fraction(state, 0.75);
    case FELT_BAND_MEDIUM:
    case FELT_BAND_MARGINAL:
      /* Showdown value, but not enough of it to build a pot with. */
      return felt_call_or_check(state);
    case FELT_BAND_AIR:
    default:
      if (has_draw) {
        return felt_call_or_check(state);
      }
      if ((state->decision_random & UINT64_C(1)) != 0U) {
        return felt_raise_to_pot_fraction(state, 0.75);
      }
      return felt_check_or_fold(state);
  }
}

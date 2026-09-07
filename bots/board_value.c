#include "board_value.h"

#include <stdbool.h>

/* Hand class on its own, before the board gets a say. */
static int base_points(const FeltMadeHand* made) {
  switch (made->category) {
    case FELT_MADE_HIGH_CARD:
      return 4;
    case FELT_MADE_ONE_PAIR:
      switch (made->pair_relation) {
        case FELT_PAIR_OVERPAIR:
          return 54;
        case FELT_PAIR_TOP:
          return 44;
        case FELT_PAIR_MIDDLE:
          return 26;
        case FELT_PAIR_BOTTOM:
          return 19;
        case FELT_PAIR_UNDERPAIR:
          return 17;
        default:
          return 11; /* the board is paired and we hold only a kicker */
      }
    case FELT_MADE_TWO_PAIR:
      switch (made->two_pair_kind) {
        case FELT_TWO_PAIR_OVER:
          return 68;
        case FELT_TWO_PAIR_BOTH_HOLE_CARDS:
          return 64;
        case FELT_TWO_PAIR_MIDDLE:
          return 54;
        case FELT_TWO_PAIR_UNDER:
          return 48;
        default:
          return 20; /* both pairs are on the board; everyone has them */
      }
    case FELT_MADE_TRIPS:
      return made->is_set ? 80 : 74;
    case FELT_MADE_STRAIGHT:
      return 86;
    case FELT_MADE_FLUSH:
      return 90;
    case FELT_MADE_FULL_HOUSE:
      return 95;
    default:
      return 100;
  }
}

/*
 * What the board takes back. A threat only costs a hand points if the hand is
 * not already ahead of it: a flush is not frightened by three of a suit, and
 * nothing below a full house is comfortable on a paired board.
 */
static int board_penalty(const FeltMadeHand* made,
                         const FeltBoardTexture* texture) {
  int penalty = 0;

  if (made->category < FELT_MADE_FLUSH) {
    if (texture->flush_on_board) {
      penalty += 34;
    } else if (texture->max_suit_count >= 4) {
      penalty += 20;
    } else if (texture->max_suit_count == 3) {
      penalty += 7;
    }
  }

  if (made->category < FELT_MADE_STRAIGHT) {
    if (texture->straight_on_board) {
      penalty += 30;
    } else if (texture->max_cards_in_five_rank_window >= 4) {
      penalty += 13;
    } else if (texture->max_cards_in_five_rank_window == 3) {
      penalty += 4;
    }
  }

  if (made->category < FELT_MADE_FULL_HOUSE) {
    if (texture->quads_on_board) {
      penalty += 40;
    } else if (texture->trips_on_board) {
      penalty += 18;
    } else if (texture->pair_count >= 1) {
      penalty += 9;
    }
  }

  return penalty;
}

static FeltHandBand band_of(int points) {
  if (points >= 74) return FELT_BAND_NUTTED;
  if (points >= 42) return FELT_BAND_STRONG;
  if (points >= 28) return FELT_BAND_MEDIUM;
  if (points >= 14) return FELT_BAND_MARGINAL;
  return FELT_BAND_AIR;
}

FeltHandValue felt_board_relative_value(const FeltMadeHand* made,
                                        const FeltBoardTexture* texture) {
  FeltHandValue value = {0};
  if (made == NULL || texture == NULL || !made->valid || !texture->valid) {
    return value;
  }
  int points = base_points(made) - board_penalty(made, texture);
  if (points < 0) points = 0;
  if (points > 100) points = 100;
  value.valid = true;
  value.points = points;
  value.band = band_of(points);
  return value;
}

int felt_call_price_percent(const FeltGameState* state) {
  if (state == NULL || state->to_call <= 0) {
    return 0;
  }
  const FeltChips after = state->pot + state->to_call;
  if (after <= 0) {
    return 100;
  }
  return (int)((100 * state->to_call) / after);
}

int felt_draw_equity_percent(const FeltGameState* state,
                             const FeltDraws* draws) {
  if (state == NULL || draws == NULL || !draws->valid) {
    return 0;
  }
  int outs = (int)draws->improving_next_cards;
  if (outs <= 0) {
    return 0;
  }
  /* The rule of four and two: on the flop each out is worth about four
   * percent because two cards are still to come, on the turn about two. The
   * flop figure over-rates a draw that will face another bet, and under-rates
   * the implied odds of hitting one two hundred big blinds deep; at this stack
   * depth those two errors point in opposite directions and roughly cancel. */
  const int cards = state->street == FELT_STREET_FLOP ? 2 : 1;
  int equity = outs * 2 * cards;
  if (equity > 95) equity = 95;
  return equity;
}

bool felt_draw_price_is_right(const FeltGameState* state,
                              const FeltDraws* draws) {
  if (state == NULL || draws == NULL || state->to_call <= 0) {
    return false;
  }
  return felt_draw_equity_percent(state, draws) >=
         felt_call_price_percent(state);
}

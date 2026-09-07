#include "board_value.h"

#include <stdbool.h>
#include <stdint.h>

#define FELT_RANK_TEN UINT8_C(8)

static uint8_t card_rank(FeltCard card) {
  return (uint8_t)(card / UINT8_C(4));
}

static uint8_t high_hole_rank(const FeltGameState* state) {
  const uint8_t first = card_rank(state->hole[0]);
  const uint8_t second = card_rank(state->hole[1]);
  return first > second ? first : second;
}

static int kicker_points(uint8_t rank) {
  if (rank >= UINT8_C(12)) return 8;
  return (int)((rank * UINT8_C(8) + UINT8_C(6)) / UINT8_C(12));
}

static FeltKickerBand kicker_band(uint8_t rank) {
  return rank >= FELT_RANK_TEN ? FELT_KICKER_STRONG : FELT_KICKER_WEAK;
}

static uint8_t trips_kicker(const FeltGameState* state,
                            const FeltBoardTexture* texture) {
  const uint8_t first = card_rank(state->hole[0]);
  const uint8_t second = card_rank(state->hole[1]);
  if (texture->trips_on_board) {
    return first > second ? first : second;
  }
  uint8_t board_counts[13] = {0};
  for (uint8_t index = 0; index < state->board_count; ++index) {
    ++board_counts[card_rank(state->board[index])];
  }
  if (board_counts[first] >= UINT8_C(2)) return second;
  if (board_counts[second] >= UINT8_C(2)) return first;
  return first > second ? first : second;
}

static bool board_is_full_house(const FeltGameState* state,
                                const FeltBoardTexture* texture) {
  return state->board_count == UINT8_C(5) && texture->trips_on_board &&
         texture->pair_count >= UINT8_C(1);
}

/* Hand class on its own, before the board gets a say. */
static int base_points(const FeltGameState* state,
                       const FeltMadeHand* made,
                       const FeltBoardTexture* texture,
                       FeltKickerBand* kicker,
                       bool* plays_board) {
  *kicker = FELT_KICKER_NONE;
  *plays_board = made->plays_board;
  switch (made->category) {
    case FELT_MADE_HIGH_CARD:
      return 4;
    case FELT_MADE_ONE_PAIR:
      switch (made->pair_relation) {
        case FELT_PAIR_OVERPAIR:
          return 54;
        case FELT_PAIR_TOP:
          *kicker = kicker_band(made->hole_kicker_rank);
          return 40 + kicker_points(made->hole_kicker_rank);
        case FELT_PAIR_MIDDLE:
          return 26;
        case FELT_PAIR_BOTTOM:
          return 19;
        case FELT_PAIR_UNDERPAIR:
          return 17;
        default:
          *kicker = kicker_band(made->hole_kicker_rank);
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
      if (made->is_set) return 80;
      {
        const uint8_t rank = trips_kicker(state, texture);
        *kicker = kicker_band(rank);
        return *kicker == FELT_KICKER_STRONG ? 74 : 68;
      }
    case FELT_MADE_STRAIGHT:
      return 86;
    case FELT_MADE_FLUSH:
      return 90;
    case FELT_MADE_FULL_HOUSE:
      if (board_is_full_house(state, texture) && made->plays_board) {
        *kicker = FELT_KICKER_PLAYS_BOARD;
        return 68;
      }
      return 95;
    case FELT_MADE_QUADS:
      if (texture->quads_on_board) {
        if (made->plays_board) {
          *kicker = FELT_KICKER_PLAYS_BOARD;
          return 74;
        }
        const uint8_t rank = high_hole_rank(state);
        *kicker = kicker_band(rank);
        return 74 + kicker_points(rank);
      }
      return 100;
    default: /* straight flush */
      return 100;
  }
}

static bool uses_board_pair(const FeltMadeHand* made,
                            const FeltBoardTexture* texture) {
  if (texture->quads_on_board) return true;
  if (made->category == FELT_MADE_TRIPS &&
      (made->is_trips || texture->trips_on_board)) {
    return true;
  }
  if (made->category == FELT_MADE_TWO_PAIR && texture->pair_count > 0U) {
    /* Every two-pair subtype except BOTH_HOLE_CARDS necessarily uses a pair
     * supplied by the board. A hand such as AK on AK772 still makes its two
     * best pairs privately, so the extra board pair remains a real threat. */
    return made->two_pair_kind != FELT_TWO_PAIR_BOTH_HOLE_CARDS;
  }
  return made->category == FELT_MADE_ONE_PAIR &&
         made->pair_relation == FELT_PAIR_NONE && texture->pair_count > 0U;
}

/*
 * What the board takes back. A threat only costs a hand points if the hand is
 * not already ahead of it: a flush is not frightened by three of a suit, and
 * nothing below a full house is comfortable on a paired board.
 */
static int board_penalty(const FeltGameState* state,
                         const FeltMadeHand* made,
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
  } else if (made->category == FELT_MADE_STRAIGHT) {
    /* A straight on the board is shared; one private card on a four-straight
     * board still makes a much less exclusive straight than two private
     * cards on a disconnected board. */
    if (texture->straight_on_board && made->plays_board) {
      penalty += 30;
    } else if (texture->straight_on_board ||
               texture->max_cards_in_five_rank_window >= 4U) {
      penalty += 13;
    }
  }

  if (made->category < FELT_MADE_FULL_HOUSE &&
      !uses_board_pair(made, texture)) {
    if (texture->quads_on_board) {
      penalty += 40;
    } else if (texture->trips_on_board) {
      penalty += 18;
    } else if (texture->pair_count >= 1) {
      penalty += 9;
    }
  } else if (made->category == FELT_MADE_FULL_HOUSE &&
             texture->pair_count >= 2U &&
             !board_is_full_house(state, texture)) {
    /* A full house completed on a two-pair board has many more neighbouring
     * full houses than one made on an unpaired board. */
    penalty += 9;
  }

  return penalty;
}

static bool has_backdoor(const FeltGameState* state, const FeltDraws* draws) {
  if (state->street != FELT_STREET_FLOP ||
      (draws->flags & FELT_DRAW_OVERCARDS) == 0U) {
    return false;
  }
  uint8_t suits[4] = {0};
  ++suits[state->hole[0] % UINT8_C(4)];
  ++suits[state->hole[1] % UINT8_C(4)];
  for (uint8_t index = 0; index < state->board_count; ++index) {
    ++suits[state->board[index] % UINT8_C(4)];
  }
  for (uint8_t suit = 0; suit < UINT8_C(4); ++suit) {
    const bool private_suit = state->hole[0] % UINT8_C(4) == suit ||
                              state->hole[1] % UINT8_C(4) == suit;
    if (private_suit && suits[suit] == UINT8_C(3)) return true;
  }
  return false;
}

static int draw_points(const FeltGameState* state,
                       const FeltMadeHand* made,
                       const FeltDraws* draws,
                       FeltDrawClass* draw_class) {
  *draw_class = FELT_DRAW_CLASS_NONE;
  if (state->street == FELT_STREET_RIVER || !draws->valid) return 0;

  const bool flush = (draws->flags & FELT_DRAW_FLUSH) != 0U;
  const bool straight =
      (draws->flags & (FELT_DRAW_OPEN_ENDED | FELT_DRAW_DOUBLE_GUTSHOT)) != 0U;
  const bool pair_flush = flush && made->category == FELT_MADE_ONE_PAIR;
  int points = 0;
  if ((flush && straight) || pair_flush) {
    *draw_class = FELT_DRAW_CLASS_COMBO;
    points = 48;
  } else if (flush) {
    *draw_class = FELT_DRAW_CLASS_FLUSH;
    points = 32;
  } else if (straight) {
    *draw_class = FELT_DRAW_CLASS_OPEN_ENDED;
    points = 28;
  } else if ((draws->flags & FELT_DRAW_GUTSHOT) != 0U) {
    *draw_class = FELT_DRAW_CLASS_GUTSHOT;
    points = 14;
  } else if (has_backdoor(state, draws)) {
    *draw_class = FELT_DRAW_CLASS_BACKDOOR;
    points = 10;
  }
  if (state->street == FELT_STREET_TURN) points /= 2;
  return points;
}

#define NUTTED_POINTS 74
#define STRONG_POINTS 42
#define MEDIUM_POINTS 28
#define MARGINAL_POINTS 14

/* Facing a raise rather than an opening bet, the two value bands cost more.
 * A dry set stops being nutted and becomes a hand that calls; an overpair
 * stops being strong and becomes a hand that needs a price. */
#define FACING_RAISE_NUTTED_BUMP 10
#define FACING_RAISE_STRONG_BUMP 16

FeltHandBand felt_band_for_points(int points, bool facing_raise) {
  const int nutted =
      NUTTED_POINTS + (facing_raise ? FACING_RAISE_NUTTED_BUMP : 0);
  const int strong =
      STRONG_POINTS + (facing_raise ? FACING_RAISE_STRONG_BUMP : 0);
  if (points >= nutted) return FELT_BAND_NUTTED;
  if (points >= strong) return FELT_BAND_STRONG;
  if (points >= MEDIUM_POINTS) return FELT_BAND_MEDIUM;
  if (points >= MARGINAL_POINTS) return FELT_BAND_MARGINAL;
  return FELT_BAND_AIR;
}

FeltHandValue felt_board_relative_value(const FeltGameState* state,
                                        const FeltMadeHand* made,
                                        const FeltDraws* draws,
                                        const FeltBoardTexture* texture) {
  FeltHandValue value = {0};
  if (state == NULL || made == NULL || draws == NULL || texture == NULL ||
      !made->valid || !draws->valid || !texture->valid) {
    return value;
  }
  value.made_points =
      base_points(state, made, texture, &value.kicker, &value.plays_board);
  value.draw_points = draw_points(state, made, draws, &value.draw_class);
  value.board_penalty = board_penalty(state, made, texture);
  int points = value.made_points > value.draw_points
                   ? value.made_points
                   : value.draw_points;
  points -= value.board_penalty;
  if (points < 0) points = 0;
  if (points > 100) points = 100;
  value.valid = true;
  value.points = points;
  value.band = felt_band_for_points(points, false);
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

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


/*
 * What the board makes on its own.
 *
 * Every hand these bots got badly wrong was a board-made hand: two pair on
 * 7-9-9 holding a seven, trip queens on QQQ holding nothing, trip eights on
 * 888 with an ace. In each case the class was scored as though we had made
 * it, when the board had, and the penalty for the paired board was waived
 * precisely because our hand used that pair. The class is worth nothing when
 * everybody has it; what is left is whatever our own cards add.
 */
typedef struct BoardProfile {
  uint8_t counts[13];
  uint8_t pair_rank;  /* the highest rank appearing exactly twice, or 255 */
  uint8_t trips_rank; /* the highest rank appearing three or more, or 255 */
  FeltMadeCategory category;
} BoardProfile;

static BoardProfile board_profile(const FeltGameState* state,
                                  const FeltBoardTexture* texture) {
  BoardProfile profile;
  for (uint8_t rank = 0; rank < 13U; ++rank) profile.counts[rank] = 0;
  profile.pair_rank = 255U;
  profile.trips_rank = 255U;
  for (uint8_t index = 0; index < state->board_count; ++index) {
    ++profile.counts[card_rank(state->board[index])];
  }
  for (uint8_t rank = 0; rank < 13U; ++rank) {
    if (profile.counts[rank] == 2U && profile.pair_rank == 255U) {
      profile.pair_rank = rank;
    }
    if (profile.counts[rank] >= 3U && profile.trips_rank == 255U) {
      profile.trips_rank = rank;
    }
  }
  /* Highest wins, so scan downward for the ranks that matter. */
  for (uint8_t rank = 13U; rank-- > 0;) {
    if (profile.counts[rank] == 2U) { profile.pair_rank = rank; break; }
  }
  for (uint8_t rank = 13U; rank-- > 0;) {
    if (profile.counts[rank] >= 3U) { profile.trips_rank = rank; break; }
  }

  if (texture->quads_on_board) {
    profile.category = FELT_MADE_QUADS;
  } else if (texture->trips_on_board && texture->pair_count >= 1U) {
    profile.category = FELT_MADE_FULL_HOUSE;
  } else if (texture->flush_on_board) {
    profile.category = FELT_MADE_FLUSH;
  } else if (texture->straight_on_board) {
    profile.category = FELT_MADE_STRAIGHT;
  } else if (texture->trips_on_board) {
    profile.category = FELT_MADE_TRIPS;
  } else if (texture->pair_count >= 2U) {
    profile.category = FELT_MADE_TWO_PAIR;
  } else if (texture->pair_count == 1U) {
    profile.category = FELT_MADE_ONE_PAIR;
  } else {
    profile.category = FELT_MADE_HIGH_CARD;
  }
  return profile;
}

/* How many board ranks sit above a rank of ours. Each one is a card an
 * opponent can hold to make the same hand with a better kicker or a better
 * second pair. */
static int board_ranks_above(const BoardProfile* profile, uint8_t rank) {
  int above = 0;
  for (uint8_t index = (uint8_t)(rank + 1U); index < 13U; ++index) {
    if (profile->counts[index] > 0U) above++;
  }
  return above;
}

/*
 * The board already makes this class, so the class is shared and only our own
 * cards separate us. A kicker contest, and a bad one: our best card has to
 * beat every card an opponent might hold.
 */
#define BOARD_HAND_PLAYS_BOARD 6
#define BOARD_HAND_BASE 10

static int board_hand_points(const FeltGameState* state,
                             const FeltMadeHand* made,
                             FeltKickerBand* kicker) {
  if (made->plays_board) {
    *kicker = FELT_KICKER_PLAYS_BOARD;
    return BOARD_HAND_PLAYS_BOARD;
  }
  const uint8_t rank = high_hole_rank(state);
  *kicker = kicker_band(rank);
  return BOARD_HAND_BASE + 2 * kicker_points(rank);
}

/* Which rank we contributed to a two pair whose other pair is the board's. */
static uint8_t private_pair_rank(const FeltGameState* state,
                                 const BoardProfile* profile) {
  const uint8_t first = card_rank(state->hole[0]);
  const uint8_t second = card_rank(state->hole[1]);
  if (first == second) return first;
  const bool first_pairs = profile->counts[first] == 1U;
  const bool second_pairs = profile->counts[second] == 1U;
  if (first_pairs && second_pairs) return first > second ? first : second;
  if (first_pairs) return first;
  if (second_pairs) return second;
  return first > second ? first : second;
}

/* Hand class on its own, before the board gets a say. */
static int base_points(const FeltGameState* state,
                       const FeltMadeHand* made,
                       const FeltBoardTexture* texture,
                       FeltKickerBand* kicker,
                       bool* plays_board) {
  *kicker = FELT_KICKER_NONE;
  *plays_board = made->plays_board;

  const BoardProfile profile = board_profile(state, texture);

  /*
   * If the board makes this class by itself, we did not make it. Trips on a
   * QQQ board, two pair on a double-paired board, a straight or flush lying
   * there for anyone -- the class is common property and only a kicker is
   * ours. This is checked before anything else, because every rule below
   * assumes the hand is at least partly ours.
   */
  if (made->category == FELT_MADE_TRIPS && texture->trips_on_board) {
    /*
     * The trips are the board's, so our cards can only kick. Straights and
     * flushes are left alone: a board straight is shared, but sharing it is a
     * chop rather than a kicker fight, and the penalties already say so.
     * Pairs and two pair are left to the rules below, which know the
     * difference between a pair of ours and a pair of the board's -- tens on
     * 7-9-9-3-3 really do beat the board's own two pair.
     */
    return board_hand_points(state, made, kicker);
  }

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
      if (profile.pair_rank != 255U &&
          made->two_pair_kind != FELT_TWO_PAIR_BOTH_HOLE_CARDS) {
        /*
         * One of the two pairs is the board's, so the kit's kind -- which
         * compares our pair with the board ranks left over after the best two
         * pairs -- can call a hand "over the board" when there is nothing
         * left to be over. On 7-9-9 a seven was scoring 68. What matters is
         * where our own pair sits: above the board's pair or below it, and
         * how many board ranks and pocket pairs are above it either way.
         */
        const uint8_t ours = private_pair_rank(state, &profile);
        const int above = board_ranks_above(&profile, ours);
        int points = ours > profile.pair_rank ? 64 : 48;
        /* Nothing on the board outranks our pair: the best two pair the board
         * allows, and only trips beat it. */
        if (above == 0 && ours > profile.pair_rank) points += 4;
        /* Every rank between ours and the board's pair is a pocket pair that
         * makes the same two pair with a better half. */
        if (profile.pair_rank > ours) {
          points -= 2 * (int)(profile.pair_rank - ours - 1U);
        }
        points -= 3 * above;
        return points < 20 ? 20 : points;
      }
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
        return BOARD_HAND_PLAYS_BOARD;
      }
      /*
       * Full houses are not one hand. The trips can be ours or the board's,
       * and when they are the board's, every opponent holding a bigger pair
       * has the same hand and beats it.
       */
      if (texture->trips_on_board) {
        /* The trips are common; our pair is the whole of our edge. */
        const uint8_t ours = private_pair_rank(state, &profile);
        int points = 74 - 3 * board_ranks_above(&profile, ours);
        return points < 30 ? 30 : points;
      }
      if (made->is_set) return 95;  /* our own set filled by the board pair */
      {
        /*
         * One hole card made trips out of a board pair. Which pair decides
         * everything: trips of the higher one is the best house the board
         * allows, trips of the lower one loses to the house made from the
         * other pair, which is a card far more opponents hold.
         */
        uint8_t ours = 255U;
        const uint8_t first = card_rank(state->hole[0]);
        const uint8_t second = card_rank(state->hole[1]);
        if (profile.counts[first] >= 2U) ours = first;
        if (profile.counts[second] >= 2U &&
            (ours == 255U || second > ours)) {
          ours = second;
        }
        uint8_t other = 255U;
        for (uint8_t rank = 13U; rank-- > 0;) {
          if (rank != ours && profile.counts[rank] >= 2U) { other = rank; break; }
        }
        if (ours == 255U || other == 255U || ours > other) return 95;
        return 82;
      }
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
  if (made->category == FELT_MADE_TWO_PAIR && texture->pair_count == 1U) {
    /* One board pair, and we are using it: it is not also a threat. Two board
     * pairs and we can only use one, so the other still is. A hand such as AK
     * on AK772 makes both its pairs privately and is charged as well. */
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

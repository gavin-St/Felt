#ifndef FELT_BOT_KIT_H
#define FELT_BOT_KIT_H

#include "felt/bot_api.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FELT_NO_RANK UINT8_C(255)
#define FELT_INVALID_PREFLOP_CLASS UINT16_MAX

/* Ordered from weakest to strongest standard made category. */
typedef enum FeltMadeCategory {
  FELT_MADE_HIGH_CARD = 0,
  FELT_MADE_ONE_PAIR = 1,
  FELT_MADE_TWO_PAIR = 2,
  FELT_MADE_TRIPS = 3,
  FELT_MADE_STRAIGHT = 4,
  FELT_MADE_FLUSH = 5,
  FELT_MADE_FULL_HOUSE = 6,
  FELT_MADE_QUADS = 7,
  FELT_MADE_STRAIGHT_FLUSH = 8
} FeltMadeCategory;

typedef enum FeltPairRelation {
  FELT_PAIR_NONE = 0,
  FELT_PAIR_UNDERPAIR = 1,
  FELT_PAIR_BOTTOM = 2,
  FELT_PAIR_MIDDLE = 3,
  FELT_PAIR_TOP = 4,
  FELT_PAIR_OVERPAIR = 5
} FeltPairRelation;

/* Ordered two-pair strength bands for simple heuristic policies. The private
 * pair is compared with board ranks outside the best two pairs. */
typedef enum FeltTwoPairKind {
  FELT_TWO_PAIR_NONE = 0,
  FELT_TWO_PAIR_BOARD_ONLY = 1,
  FELT_TWO_PAIR_UNDER = 2,
  FELT_TWO_PAIR_MIDDLE = 3,
  FELT_TWO_PAIR_OVER = 4,
  FELT_TWO_PAIR_BOTH_HOLE_CARDS = 5
} FeltTwoPairKind;

typedef struct FeltMadeHand {
  /* False means the input was null, duplicated, invalid, or not postflop. */
  bool valid;
  /* OMPEval rank; larger values are stronger. */
  uint16_t rank;
  FeltMadeCategory category;
  FeltPairRelation pair_relation;
  FeltTwoPairKind two_pair_kind;
  /* The unpaired hole card for board-pair hands; 255 when not applicable. */
  uint8_t hole_kicker_rank;
  bool is_set;
  bool is_trips;
  /* These two fields are meaningful only on the river. */
  bool plays_board;
  bool improves_board;
} FeltMadeHand;

typedef enum FeltDrawFlag {
  FELT_DRAW_NONE = 0,
  FELT_DRAW_OVERCARDS = 1 << 0,
  FELT_DRAW_GUTSHOT = 1 << 1,
  FELT_DRAW_OPEN_ENDED = 1 << 2,
  FELT_DRAW_DOUBLE_GUTSHOT = 1 << 3,
  FELT_DRAW_FLUSH = 1 << 4
} FeltDrawFlag;

typedef struct FeltDraws {
  /* River inputs are valid but always have no live draws. */
  bool valid;
  uint32_t flags;
  /* Counts unique unseen cards, not memorized nominal outs. */
  uint8_t improving_next_cards;
  uint8_t straight_next_cards;
  uint8_t flush_next_cards;
  bool nut_flush_draw;
} FeltDraws;

typedef struct FeltBoardTexture {
  /* False means the input was null, duplicated, invalid, or not postflop. */
  bool valid;
  uint8_t high_rank;
  uint8_t broadway_count;
  uint8_t distinct_rank_count;
  uint8_t max_suit_count;
  uint8_t max_cards_in_five_rank_window;
  /* Number of ranks appearing exactly twice. */
  uint8_t pair_count;
  bool trips_on_board;
  bool quads_on_board;
  bool straight_on_board;
  bool flush_on_board;
} FeltBoardTexture;

typedef enum FeltPreflopSpot {
  FELT_PREFLOP_SPOT_INVALID = 0,
  FELT_PREFLOP_SB_FIRST_IN = 1,
  FELT_PREFLOP_BB_VS_SB_LIMP = 2,
  FELT_PREFLOP_BB_VS_SMALL_RAISE = 3,
  FELT_PREFLOP_SB_VS_SMALL_RAISE = 4,
  FELT_PREFLOP_VS_THREE_BET = 5,
  FELT_PREFLOP_VS_FOUR_BET = 6,
  FELT_PREFLOP_VS_FIVE_BET = 7,
  FELT_PREFLOP_VS_ALL_IN_SIZED_RAISE = 8
} FeltPreflopSpot;

typedef enum FeltPreflopChartAction {
  FELT_PREFLOP_CHART_FOLD = 0,
  /* Check, limp, or call according to the legal state. */
  FELT_PREFLOP_CHART_PASSIVE = 1,
  FELT_PREFLOP_CHART_RAISE_VALUE = 2,
  FELT_PREFLOP_CHART_RAISE_BLUFF = 3,
  FELT_PREFLOP_CHART_ALL_IN = 4
} FeltPreflopChartAction;

typedef struct FeltPreflopDecision {
  bool valid;
  FeltPreflopSpot spot;
  FeltPreflopChartAction chart_action;
  /* Fixed total raise size in hundredths of a big blind; zero if unused. */
  uint16_t raise_to_bb_x100;
} FeltPreflopDecision;

/* Call once from felt_bot_name(), outside the decision timer. */
void felt_bot_kit_warmup(void);

/* Both classifiers accept flop, turn, or river inputs (board_count 3..5). */
FeltMadeHand felt_made_hand(const FeltCard hole[2],
                            const FeltCard* board,
                            uint8_t board_count);

FeltDraws felt_draws(const FeltCard hole[2],
                     const FeltCard* board,
                     uint8_t board_count);

FeltBoardTexture felt_board_texture(const FeltCard* board,
                                    uint8_t board_count);

/* River board-only straight or better: fraction of legal opponent holdings
 * that tie, in basis points, with both hero cards removed. Returns -1 for
 * other/invalid inputs. This is a uniform-combination baseline, not a read
 * of the opponent's betting range. No legal opponent holding can be worse. */
int felt_board_chop_share_basis_points(const FeltCard hole[2],
                                       const FeltCard board[5]);

/* Uses the same canonical mapping as solved_all_in: pairs on the diagonal,
 * suited at low*13+high, offsuit at high*13+low. */
uint16_t felt_preflop_class(FeltCard first, FeltCard second);

FeltPreflopChartAction felt_preflop_baseline_lookup(FeltPreflopSpot spot,
                                                     FeltCard first,
                                                     FeltCard second);

/* Built-in 100 bb heads-up baseline. Facing-raise charts are selected by the
 * additional amount required to call: <6, 6..<16, 16..<22, 22..<45, or >=45 bb. The
 * direct action helper always returns a legal fallback. */
FeltPreflopDecision felt_preflop_baseline_decision(
    const FeltGameState* state);
FeltAction felt_preflop_baseline_action(const FeltGameState* state);

/* Legal action helpers. Aggressive helpers call/check when raising is not
 * available and clamp their total-contribution target to legal bounds. A pot
 * fraction is a raise after first calling, or a simple bet when to_call=0. */
/*
 * Betting context read out of the history, so no bot has to walk it again.
 *
 * The big blind is the amount posted before the flop. It is the only place the
 * size of the game appears in a game state -- there is no blind field -- and
 * without it a bot cannot tell a 2 bb open from a 2 chip one.
 *
 * The raise count is voluntary raises only: one after an open, two after a
 * three-bet, and so on. Posting a blind is not a raise, however much it looks
 * like a bet in the history.
 *
 * Both were specified in BOT_KIT.md and never written. Bots calling them
 * compiled anyway, because C11 lets an unknown function be assumed to return
 * int, and only failed at the link.
 */
FeltChips felt_big_blind(const FeltGameState* state);
uint8_t felt_preflop_raise_count(const FeltGameState* state);

FeltAction felt_check_or_fold(const FeltGameState* state);
FeltAction felt_call_or_check(const FeltGameState* state);
FeltAction felt_raise_to_pot_fraction(const FeltGameState* state,
                                      double fraction);
FeltAction felt_raise_to_multiple(const FeltGameState* state,
                                  uint32_t multiple);
FeltAction felt_all_in(const FeltGameState* state);

/* Common postflop bluff gate. Use the effective chips left after matching
 * the current bet, divided by the pot after calling. Below 0.5 SPR there is
 * too little left to threaten; an all-in opponent cannot fold to a raise.
 * Exactly 0.5 is allowed. Value sizing and ordinary calls are unaffected. */
static inline bool felt_bluff_allowed(const FeltGameState* state) {
  if (state == NULL || state->street == FELT_STREET_PREFLOP ||
      (state->legal_actions & FELT_LEGAL_RAISE_TO) == 0U ||
      state->my_stack <= state->to_call || state->opp_stack <= 0 ||
      state->max_raise_to <= state->opp_street_contribution) return false;
  const FeltChips ours = state->my_stack - state->to_call;
  const FeltChips effective = ours < state->opp_stack ? ours : state->opp_stack;
  const FeltChips pot = state->pot + state->to_call;
  return pot > 0 && effective >= pot / 2 + pot % 2;
}

/* A failed bluff never silently becomes a call through a sizing fallback. */
static inline FeltAction felt_bluff_action(const FeltGameState* state,
                                           FeltAction candidate) {
  if (!felt_bluff_allowed(state) || candidate.type != FELT_ACTION_RAISE_TO) {
    return felt_check_or_fold(state);
  }
  candidate.flags |= FELT_ACTION_FLAG_BLUFF;
  return candidate;
}

/*
 * How much of a flush is actually the player's, on a board that is doing some
 * or all of the work.
 *
 * Flushes are the one category where the same five-card name covers a monster
 * and a bluff catcher. With three of the suit on the board both hole cards are
 * in the hand and it is wholly the player's. With four, one card is, and which
 * card it is decides everything: the ace is the nuts and the three beats only
 * the players who missed entirely. With five, everybody already holds the
 * board's flush and a card enters the hand only by beating the lowest of them.
 *
 * Strength is counted in higher cards of the suit still unseen, not in raw
 * rank. A king is the nuts when the ace is lying on the board, and a queen is
 * second best when the king is: ranks above yours that everyone can see are in
 * everyone's hand equally and beat nobody. Two or fewer unseen -- the ace,
 * king and queen class -- is a hand worth building a pot with. Below that it
 * is real, and it is a call, and it is not a reason to raise.
 */
typedef enum FeltFlushTier {
  FELT_FLUSH_TIER_BOARD = 0, /* not the player's hand at all */
  FELT_FLUSH_TIER_SHOWDOWN,  /* the player's, and worth a call */
  FELT_FLUSH_TIER_STRONG     /* the player's, and worth a raise */
} FeltFlushTier;

static inline FeltFlushTier felt_flush_tier(const FeltCard hole[2],
                                            const FeltCard* board,
                                            uint8_t board_count) {
  if (hole == NULL || board == NULL) {
    return FELT_FLUSH_TIER_BOARD;
  }
  uint8_t suit = 4U;
  uint8_t on_board = 0;
  for (uint8_t candidate = 0; candidate < 4U; ++candidate) {
    uint8_t count = 0;
    for (uint8_t index = 0; index < board_count; ++index) {
      if ((uint8_t)(board[index] & 3U) == candidate) ++count;
    }
    if (count > on_board) {
      on_board = count;
      suit = candidate;
    }
  }
  if (suit == 4U) return FELT_FLUSH_TIER_BOARD;

  /* Three on the board means the other two are the player's own. */
  if (on_board <= 3U) return FELT_FLUSH_TIER_STRONG;

  uint8_t best = 13U;
  for (uint8_t index = 0; index < 2U; ++index) {
    if ((uint8_t)(hole[index] & 3U) != suit) continue;
    const uint8_t rank = (uint8_t)(hole[index] >> 2);
    if (best == 13U || rank > best) best = rank;
  }
  if (best == 13U) return FELT_FLUSH_TIER_BOARD;

  if (on_board >= 5U) {
    uint8_t lowest = 13U;
    for (uint8_t index = 0; index < board_count; ++index) {
      if ((uint8_t)(board[index] & 3U) != suit) continue;
      const uint8_t rank = (uint8_t)(board[index] >> 2);
      if (rank < lowest) lowest = rank;
    }
    if (best <= lowest) return FELT_FLUSH_TIER_BOARD;
  }

  uint8_t higher_unseen = 0;
  for (uint8_t rank = (uint8_t)(best + 1U); rank < 13U; ++rank) {
    bool seen = false;
    for (uint8_t index = 0; index < board_count; ++index) {
      if ((uint8_t)(board[index] & 3U) == suit &&
          (uint8_t)(board[index] >> 2) == rank) {
        seen = true;
        break;
      }
    }
    if (!seen) ++higher_unseen;
  }
  return higher_unseen <= 2U ? FELT_FLUSH_TIER_STRONG
                             : FELT_FLUSH_TIER_SHOWDOWN;
}

/* Private value for the simple policies. Board-only hands and board trips
 * with a kicker are showdown holdings. Four/five-flushes are graded by the
 * suited card held; full houses sharing board trips are graded by the pair.
 * Low full houses and the lower house on a two-pair board stay in the
 * showdown tier. Detailed numerical scoring belongs to board_value.c. */
static inline bool felt_hand_is_own(const FeltCard hole[2],
                                    const FeltCard* board,
                                    uint8_t board_count,
                                    const FeltMadeHand* hand,
                                    const FeltBoardTexture* texture) {
  if (hand == NULL || !hand->valid) {
    return false;
  }
  if (hand->category == FELT_MADE_FLUSH) {
    return felt_flush_tier(hole, board, board_count) ==
           FELT_FLUSH_TIER_STRONG;
  }
  if (texture == NULL || !texture->valid) {
    return true;
  }
  switch (hand->category) {
    case FELT_MADE_STRAIGHT_FLUSH:
      return board_count < 5U || hand->improves_board;
    case FELT_MADE_QUADS:
      return !texture->quads_on_board;
    case FELT_MADE_FULL_HOUSE: {
      if (board_count == 5U && !hand->improves_board) return false;
      if (hole == NULL || board == NULL) return false;
      uint8_t counts[13] = {0};
      uint8_t board_counts[13] = {0};
      for (uint8_t i = 0; i < board_count; ++i) {
        ++counts[board[i] >> 2];
        ++board_counts[board[i] >> 2];
      }
      ++counts[hole[0] >> 2];
      ++counts[hole[1] >> 2];
      uint8_t trips = 13U, pair = 13U;
      for (uint8_t rank = 13U; rank-- > 0;) {
        if (counts[rank] >= 3U && trips == 13U) trips = rank;
      }
      for (uint8_t rank = 13U; rank-- > 0;) {
        if (rank != trips && counts[rank] >= 2U) { pair = rank; break; }
      }
      if (trips == 13U || pair == 13U) return false;
      if (board_counts[trips] >= 3U) {
        /* Shared trips leave our pair to do all the work. A low pocket
         * pair is a showdown hand, like a low card on a four-flush. */
        int higher_pairs = 0;
        for (uint8_t rank = pair + 1U; rank < 13U; ++rank) {
          if (rank != trips) ++higher_pairs;
        }
        return higher_pairs <= 2;
      }
      if (texture->pair_count >= 2U) {
        /* Trips from the lower board pair lose to any card matching the
         * higher pair. Keep that underfull in the showdown tier. */
        for (uint8_t rank = trips + 1U; rank < 13U; ++rank) {
          if (board_counts[rank] >= 2U) return false;
        }
      }
      return true;
    }
    case FELT_MADE_STRAIGHT:
      return board_count < 5U || hand->improves_board;
    case FELT_MADE_TRIPS:
      return hand->is_set || hand->is_trips;
    case FELT_MADE_TWO_PAIR:
      return hand->two_pair_kind == FELT_TWO_PAIR_OVER ||
             hand->two_pair_kind == FELT_TWO_PAIR_BOTH_HOLE_CARDS;
    default:
      return true;
  }
}

/*
 * Does a card of ours actually play? For a hand the board made, the five that
 * count are the board's plus whichever kickers are highest, so our card is
 * worth something only when it outranks the board card it would replace. An
 * A queen on seven-seven-seven-king-deuce plays by replacing the deuce.
 * Paired ranks never serve as kickers. A flush asks the same question of the suit: our
 * heart plays when it beats the lowest heart on the board.
 *
 * Crude on purpose. It answers whether we have anything, not how much.
 */
static inline bool felt_kicker_plays(const FeltCard hole[2],
                                     const FeltCard* board,
                                     uint8_t board_count,
                                     const FeltMadeHand* hand,
                                     const FeltBoardTexture* texture) {
  (void)texture;
  if (hole == NULL || board == NULL || hand == NULL || !hand->valid) {
    return false;
  }
  const uint8_t first = (uint8_t)(hole[0] >> 2);
  const uint8_t second = (uint8_t)(hole[1] >> 2);
  const uint8_t best = first > second ? first : second;

  /* A full house or a straight the board made uses all five of its cards, so
   * there is no kicker to win or lose: the hand is a chop and it is taken to
   * showdown either way. */
  if (hand->category == FELT_MADE_FULL_HOUSE ||
      hand->category == FELT_MADE_STRAIGHT ||
      hand->category == FELT_MADE_STRAIGHT_FLUSH) {
    return true;
  }

  /* A flush the player holds any live card of is at least a hand to show
   * down, even when the board is four to the suit and the card is a three.
   * Whether it is more than that is felt_hand_is_own's question. */
  if (hand->category == FELT_MADE_FLUSH) {
    return felt_flush_tier(hole, board, board_count) != FELT_FLUSH_TIER_BOARD;
  }

  if (board_count == 5U) return hand->improves_board;

  /* On earlier streets ignore the repeated ranks: they make the category,
   * not its kicker. With fewer than five board cards, a private card can
   * also fill an otherwise empty kicker slot. */
  uint8_t counts[13] = {0};
  for (uint8_t index = 0; index < board_count; ++index) {
    ++counts[board[index] >> 2];
  }
  uint8_t kicker_slots = hand->category == FELT_MADE_QUADS ? 1U
                         : hand->category == FELT_MADE_TRIPS ? 2U
                         : hand->category == FELT_MADE_TWO_PAIR ? 1U : 3U;
  uint8_t board_kickers = 0;
  uint8_t lowest_kicker = 13U;
  for (uint8_t index = 0; index < board_count; ++index) {
    const uint8_t rank = (uint8_t)(board[index] >> 2);
    if (counts[rank] == 1U) {
      ++board_kickers;
      if (rank < lowest_kicker) lowest_kicker = rank;
    }
  }
  const bool private_kicker = counts[best] < 2U;
  return private_kicker &&
         (board_kickers < kicker_slots || best > lowest_kicker);
}

static inline bool felt_is_top_pair_or_better(const FeltMadeHand* hand) {
  if (hand == NULL || !hand->valid) {
    return false;
  }
  if (hand->category >= FELT_MADE_TRIPS) {
    return true;
  }
  if (hand->category == FELT_MADE_TWO_PAIR) {
    return hand->two_pair_kind == FELT_TWO_PAIR_OVER ||
           hand->two_pair_kind == FELT_TWO_PAIR_BOTH_HOLE_CARDS;
  }
  return hand->category == FELT_MADE_ONE_PAIR &&
         (hand->pair_relation == FELT_PAIR_TOP ||
          hand->pair_relation == FELT_PAIR_OVERPAIR);
}

#ifdef __cplusplus
}
#endif

#endif

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
  FELT_PREFLOP_VS_MEDIUM_RAISE = 5,
  FELT_PREFLOP_VS_LARGE_RAISE = 6,
  FELT_PREFLOP_VS_ALL_IN_SIZED_RAISE = 7
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

/* Uses the same canonical mapping as solved_all_in: pairs on the diagonal,
 * suited at low*13+high, offsuit at high*13+low. */
uint16_t felt_preflop_class(FeltCard first, FeltCard second);

FeltPreflopChartAction felt_preflop_baseline_lookup(FeltPreflopSpot spot,
                                                     FeltCard first,
                                                     FeltCard second);

/* Built-in 100 bb heads-up baseline. Facing-raise charts are selected by the
 * additional amount required to call: <10, 10..<40, 40..<75, or >=75 bb. The
 * direct action helper always returns a legal fallback. */
FeltPreflopDecision felt_preflop_baseline_decision(
    const FeltGameState* state);
FeltAction felt_preflop_baseline_action(const FeltGameState* state);

/* Deliberately flawed comparison policy. It routes the first, second, and
 * later raises as small, medium, and large without looking at their size. An
 * opening all-in is therefore treated as a small raise. */
FeltPreflopDecision felt_preflop_action_count_v0_decision(
    const FeltGameState* state);
FeltAction felt_preflop_action_count_v0_action(const FeltGameState* state);

/* Legal action helpers. Aggressive helpers call/check when raising is not
 * available and clamp their total-contribution target to legal bounds. A pot
 * fraction is a raise after first calling, or a simple bet when to_call=0. */
FeltAction felt_check_or_fold(const FeltGameState* state);
FeltAction felt_call_or_check(const FeltGameState* state);
FeltAction felt_raise_to_pot_fraction(const FeltGameState* state,
                                      double fraction);
FeltAction felt_raise_to_multiple(const FeltGameState* state,
                                  uint32_t multiple);
FeltAction felt_all_in(const FeltGameState* state);

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

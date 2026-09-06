#include "felt/bot_kit.h"

int main(void) {
  const FeltCard hole[2] = {51U, 45U}; /* Ah Kd */
  const FeltCard board[3] = {48U, 22U, 3U}; /* Ac 7s 2h */
  felt_bot_kit_warmup();
  const FeltMadeHand hand = felt_made_hand(hole, board, 3U);
  const FeltDraws draws = felt_draws(hole, board, 3U);
  const FeltBoardTexture texture = felt_board_texture(board, 3U);

  if (!hand.valid || hand.category != FELT_MADE_ONE_PAIR ||
      hand.pair_relation != FELT_PAIR_TOP || hand.hole_kicker_rank != 11U) {
    return 1;
  }
  if (!texture.valid || texture.high_rank != 12U ||
      texture.distinct_rank_count != 3U) {
    return 2;
  }
  if (!draws.valid) {
    return 5;
  }
  if (felt_preflop_class(hole[0], hole[1]) != 167U ||
      felt_preflop_baseline_lookup(FELT_PREFLOP_SB_FIRST_IN, hole[0],
                                   hole[1]) !=
          FELT_PREFLOP_CHART_PASSIVE) {
    return 3;
  }
  if (felt_preflop_action_count_v0_decision(NULL).valid) {
    return 4;
  }
  return 0;
}

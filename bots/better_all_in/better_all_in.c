/*
 * better-all-in: shoves a broad "premium" range, folds everything else.
 *
 * The range is the union of:
 *   - pocket pairs 99 and better;
 *   - any two broadway cards (T, J, Q, K, A);
 *   - any ace;
 *   - any king.
 *
 * Ace-x and king-x dominate that union, so it is far wider than "premium"
 * suggests: 452 of 1,326 combinations, about 34% of hands, including K2o and
 * A3o. It exists to contrast with nit-all-in at the other extreme.
 */

#include "felt/bot_api.h"

/* rank = card >> 2, with 0 = deuce, 7 = nine, 8 = ten ... 11 = K, 12 = A. */
#define RANK_NINE 7U
#define RANK_TEN 8U
#define RANK_KING 11U
#define RANK_ACE 12U

static uint32_t rank_of(FeltCard card) { return (uint32_t)(card >> 2U); }

static int is_premium(const FeltGameState* state) {
  const uint32_t first = rank_of(state->hole[0]);
  const uint32_t second = rank_of(state->hole[1]);

  if (first == second) {
    return first >= RANK_NINE; /* 99+ */
  }
  if (first >= RANK_TEN && second >= RANK_TEN) {
    return 1; /* both broadway */
  }
  if (first == RANK_ACE || second == RANK_ACE) {
    return 1; /* any ace */
  }
  if (first == RANK_KING || second == RANK_KING) {
    return 1; /* any king */
  }
  return 0;
}

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char* felt_bot_name(void) { return "better-all-in"; }

FeltAction felt_bot_act(const FeltGameState* state) {
  FeltAction action = {0};

  if (is_premium(state)) {
    if ((state->legal_actions & FELT_LEGAL_RAISE_TO) != 0U) {
      action.type = FELT_ACTION_RAISE_TO;
      action.amount_to = state->max_raise_to;
      return action;
    }
    if ((state->legal_actions & FELT_LEGAL_CALL) != 0U) {
      action.type = FELT_ACTION_CALL;
      return action;
    }
  }

  action.type = (state->legal_actions & FELT_LEGAL_FOLD) != 0U
                    ? FELT_ACTION_FOLD
                    : FELT_ACTION_CHECK;
  return action;
}

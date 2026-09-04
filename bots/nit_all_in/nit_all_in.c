/*
 * nit-all-in: shoves only AA, KK and QQ. Folds everything else.
 *
 * A deliberately extreme range, useful as a fixed reference point: any bot that
 * cannot beat it is folding far too much, and its own results show how badly a
 * range this tight bleeds blinds.
 */

#include "felt/bot_api.h"

/* rank = card >> 2, with 0 = deuce ... 10 = Q, 11 = K, 12 = A. */
#define RANK_QUEEN 10U

static uint32_t rank_of(FeltCard card) { return (uint32_t)(card >> 2U); }

static int is_premium(const FeltGameState* state) {
  const uint32_t first = rank_of(state->hole[0]);
  const uint32_t second = rank_of(state->hole[1]);
  return first == second && first >= RANK_QUEEN;
}

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char* felt_bot_name(void) { return "nit-all-in"; }

FeltAction felt_bot_act(const FeltGameState* state) {
  FeltAction action = {0};

  if (is_premium(state)) {
    if ((state->legal_actions & FELT_LEGAL_RAISE_TO) != 0U) {
      action.type = FELT_ACTION_RAISE_TO;
      action.amount_to = state->max_raise_to;
      return action;
    }
    /* Cannot raise: either already facing an all-in, or out of chips. */
    if ((state->legal_actions & FELT_LEGAL_CALL) != 0U) {
      action.type = FELT_ACTION_CALL;
      return action;
    }
  }

  /* Fold is legal only when facing a bet; otherwise checking is the only way to
   * decline, so a free look is taken rather than a fold that is not available. */
  action.type = (state->legal_actions & FELT_LEGAL_FOLD) != 0U
                    ? FELT_ACTION_FOLD
                    : FELT_ACTION_CHECK;
  return action;
}

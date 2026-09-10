#include "felt/bot_api.h"

#include <stddef.h>

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char *felt_bot_name(void) { return "wasm-check-call-test"; }

FeltAction felt_bot_act(const FeltGameState *state) {
  FeltAction action = {0};
  action.type = FELT_ACTION_FOLD;
  if (state != NULL && state->abi_version == FELT_BOT_ABI_VERSION &&
      state->struct_size >= sizeof(FeltGameState) && state->pot == 321 &&
      state->hole[0] == 48 && state->board_count == 3 &&
      state->board[2] == 17 && state->history_count == 1 &&
      state->history != NULL && state->history[0].amount_to == 123 &&
      (state->legal_actions & FELT_LEGAL_CALL) != 0U) {
    action.type = FELT_ACTION_CALL;
  }
  return action;
}

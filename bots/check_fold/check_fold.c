#include "felt/bot_api.h"

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char* felt_bot_name(void) { return "check-fold"; }

FeltAction felt_bot_act(const FeltGameState* state) {
  FeltAction action = {0};
  action.type = (state->legal_actions & FELT_LEGAL_CHECK) != 0U
                    ? FELT_ACTION_CHECK
                    : FELT_ACTION_FOLD;
  return action;
}

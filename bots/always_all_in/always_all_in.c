#include "felt/bot_api.h"

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char* felt_bot_name(void) { return "always-all-in"; }

FeltAction felt_bot_act(const FeltGameState* state) {
  FeltAction action = {0};
  if ((state->legal_actions & FELT_LEGAL_RAISE_TO) != 0U) {
    action.type = FELT_ACTION_RAISE_TO;
    action.amount_to = state->max_raise_to;
  } else if ((state->legal_actions & FELT_LEGAL_CALL) != 0U) {
    action.type = FELT_ACTION_CALL;
  } else if ((state->legal_actions & FELT_LEGAL_CHECK) != 0U) {
    action.type = FELT_ACTION_CHECK;
  } else {
    action.type = FELT_ACTION_FOLD;
  }
  return action;
}

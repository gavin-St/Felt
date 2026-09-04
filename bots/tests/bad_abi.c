#include "felt/bot_api.h"

uint32_t felt_bot_abi_version(void) { return UINT32_C(999); }

const char* felt_bot_name(void) { return "bad-abi"; }

FeltAction felt_bot_act(const FeltGameState* state) {
  FeltAction action = {0};
  (void)state;
  action.type = FELT_ACTION_CHECK;
  return action;
}

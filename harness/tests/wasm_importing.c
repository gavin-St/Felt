#include "felt/bot_api.h"

__attribute__((import_module("forbidden"), import_name("host_call")))
void host_call(void);

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }
const char *felt_bot_name(void) { return "wasm-importing-test"; }

FeltAction felt_bot_act(const FeltGameState *state) {
  FeltAction action = {0};
  (void)state;
  host_call();
  action.type = FELT_ACTION_FOLD;
  return action;
}

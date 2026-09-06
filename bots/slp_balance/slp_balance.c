#include "../slp_common.h"

#include "felt/bot_kit.h"

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char* felt_bot_name(void) {
  felt_bot_kit_warmup();
  return "slp-balance";
}

FeltAction felt_bot_act(const FeltGameState* state) {
  return slp_act(state, SLP_BALANCE);
}

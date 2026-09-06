#include "../archetype_common.h"

#include "felt/bot_kit.h"

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char* felt_bot_name(void) {
  felt_bot_kit_warmup();
  return "semi-bluff-sarah";
}

FeltAction felt_bot_act(const FeltGameState* state) {
  return archetype_act(state, ARCHETYPE_SEMI_BLUFF_SARAH);
}

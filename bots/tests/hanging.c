#include "felt/bot_api.h"

#include <stdint.h>

FELT_BOT_EXPORT uint32_t felt_bot_abi_version(void) {
  return FELT_BOT_ABI_VERSION;
}

FELT_BOT_EXPORT const char *felt_bot_name(void) { return "hanging-test-bot"; }

FELT_BOT_EXPORT FeltAction felt_bot_act(const FeltGameState *state) {
  volatile uint64_t spin = state->decision_random;
  for (;;) {
    spin = spin * UINT64_C(6364136223846793005) + UINT64_C(1);
  }
}

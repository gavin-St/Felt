#include "felt/bot_api.h"

#include <signal.h>
#include <stdint.h>
#include <unistd.h>

FELT_BOT_EXPORT uint32_t felt_bot_abi_version(void) {
  return FELT_BOT_ABI_VERSION;
}

FELT_BOT_EXPORT const char *felt_bot_name(void) { return "crashing-test-bot"; }

FELT_BOT_EXPORT FeltAction felt_bot_act(const FeltGameState *state) {
  (void)state;
  (void)kill(getpid(), SIGKILL);
  return (FeltAction){FELT_ACTION_FOLD, 0, 0};
}

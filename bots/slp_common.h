#ifndef FELT_SLP_COMMON_H
#define FELT_SLP_COMMON_H

#include "felt/bot_api.h"

typedef enum SlpProfile {
  SLP_FOLD = 0,
  SLP_BLUFF = 1,
  SLP_BALANCE = 2,
  SLP_EXPLOIT_FOLD = 3
} SlpProfile;

FeltAction slp_act(const FeltGameState* state, SlpProfile profile);

#endif

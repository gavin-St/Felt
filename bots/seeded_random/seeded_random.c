#include "felt/bot_api.h"

#include <stddef.h>

static uint64_t splitmix64(uint64_t* state) {
  uint64_t value = (*state += UINT64_C(0x9e3779b97f4a7c15));
  value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31U);
}

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char* felt_bot_name(void) { return "seeded-random"; }

FeltAction felt_bot_act(const FeltGameState* state) {
  uint32_t choices[4] = {0};
  size_t choice_count = 0;
  uint64_t random_state = state->decision_random;
  FeltAction action = {0};

  if ((state->legal_actions & FELT_LEGAL_FOLD) != 0U) {
    choices[choice_count++] = FELT_ACTION_FOLD;
  }
  if ((state->legal_actions & FELT_LEGAL_CHECK) != 0U) {
    choices[choice_count++] = FELT_ACTION_CHECK;
  }
  if ((state->legal_actions & FELT_LEGAL_CALL) != 0U) {
    choices[choice_count++] = FELT_ACTION_CALL;
  }
  if ((state->legal_actions & FELT_LEGAL_RAISE_TO) != 0U) {
    choices[choice_count++] = FELT_ACTION_RAISE_TO;
  }

  if (choice_count == 0U) {
    action.type = FELT_ACTION_FOLD;
    return action;
  }

  action.type = choices[splitmix64(&random_state) % choice_count];
  if (action.type == FELT_ACTION_RAISE_TO) {
    if (state->max_raise_to < state->min_raise_to) {
      action.amount_to = state->max_raise_to;
    } else {
      const uint64_t span =
          (uint64_t)(state->max_raise_to - state->min_raise_to) + UINT64_C(1);
      action.amount_to =
          state->min_raise_to + (FeltChips)(splitmix64(&random_state) % span);
    }
  }

  return action;
}

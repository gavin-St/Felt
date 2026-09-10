#include "felt/wasm_bot_api.h"

#include <stdint.h>

static FeltWasmGameState wire_state;
static FeltActionEvent wire_history[FELT_WASM_MAX_HISTORY_EVENTS];
static FeltAction wire_action;

uint32_t felt_wasm_bot_abi_version(void) {
  return FELT_WASM_BOT_ABI_VERSION;
}

uint32_t felt_wasm_state_ptr(void) {
  return (uint32_t)(uintptr_t)&wire_state;
}

uint32_t felt_wasm_history_ptr(void) {
  return (uint32_t)(uintptr_t)wire_history;
}

uint32_t felt_wasm_history_capacity(void) {
  return FELT_WASM_MAX_HISTORY_EVENTS;
}

uint32_t felt_wasm_action_ptr(void) {
  return (uint32_t)(uintptr_t)&wire_action;
}

uint32_t felt_wasm_bot_name_ptr(void) {
  return (uint32_t)(uintptr_t)felt_bot_name();
}

void felt_wasm_act(void) {
  FeltGameState state;
  uint32_t index;

  state.abi_version = wire_state.abi_version;
  state.struct_size = sizeof(state);
  state.hole[0] = wire_state.hole[0];
  state.hole[1] = wire_state.hole[1];
  for (index = 0; index < 5; ++index) {
    state.board[index] = wire_state.board[index];
  }
  state.board_count = wire_state.board_count;
  state.street = wire_state.street;
  state.position = wire_state.position;
  state.legal_actions = wire_state.legal_actions;
  state.reserved0 = wire_state.reserved0;
  state.pot = wire_state.pot;
  state.my_stack = wire_state.my_stack;
  state.opp_stack = wire_state.opp_stack;
  state.my_street_contribution = wire_state.my_street_contribution;
  state.opp_street_contribution = wire_state.opp_street_contribution;
  state.to_call = wire_state.to_call;
  state.min_raise_to = wire_state.min_raise_to;
  state.max_raise_to = wire_state.max_raise_to;
  state.decision_cap_us = wire_state.decision_cap_us;
  state.decision_random = wire_state.decision_random;
  state.history = wire_history;
  state.history_count = wire_state.history_count;
  state.reserved1 = wire_state.reserved1;

  wire_action = felt_bot_act(&state);
}

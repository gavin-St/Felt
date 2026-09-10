#ifndef FELT_WASM_BOT_API_H
#define FELT_WASM_BOT_API_H

#include "felt/bot_api.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ABI between the harness and the trusted adapter inside a Wasm module. */
#define FELT_WASM_BOT_ABI_VERSION UINT32_C(1)
#define FELT_WASM_MAX_HISTORY_EVENTS UINT32_C(1024)

/*
 * WebAssembly uses 32-bit offsets into linear memory, so the native
 * FeltGameState history pointer cannot cross the boundary. This wire structure
 * carries the fixed fields; history lives in a separate exported buffer.
 */
typedef struct FeltWasmGameState {
  uint32_t abi_version;
  uint32_t struct_size;

  FeltCard hole[2];
  FeltCard board[5];
  uint8_t board_count;

  uint32_t street;
  uint32_t position;
  uint32_t legal_actions;
  uint32_t reserved0;

  FeltChips pot;
  FeltChips my_stack;
  FeltChips opp_stack;
  FeltChips my_street_contribution;
  FeltChips opp_street_contribution;
  FeltChips to_call;
  FeltChips min_raise_to;
  FeltChips max_raise_to;

  uint64_t decision_cap_us;
  uint64_t decision_random;

  uint32_t history_count;
  uint32_t reserved1;
} FeltWasmGameState;

/* These exports are supplied by harness/wasm/wasm_adapter.c. */
FELT_BOT_EXPORT uint32_t felt_wasm_bot_abi_version(void);
FELT_BOT_EXPORT uint32_t felt_wasm_state_ptr(void);
FELT_BOT_EXPORT uint32_t felt_wasm_history_ptr(void);
FELT_BOT_EXPORT uint32_t felt_wasm_history_capacity(void);
FELT_BOT_EXPORT uint32_t felt_wasm_action_ptr(void);
FELT_BOT_EXPORT uint32_t felt_wasm_bot_name_ptr(void);
FELT_BOT_EXPORT void felt_wasm_act(void);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(sizeof(FeltWasmGameState) == 120,
              "FeltWasmGameState ABI layout changed");
static_assert(offsetof(FeltWasmGameState, history_count) == 112,
              "FeltWasmGameState ABI layout changed");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(FeltWasmGameState) == 120,
               "FeltWasmGameState ABI layout changed");
_Static_assert(offsetof(FeltWasmGameState, history_count) == 112,
               "FeltWasmGameState ABI layout changed");
#endif

#endif

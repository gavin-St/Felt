#ifndef FELT_BOT_API_H
#define FELT_BOT_API_H

#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define FELT_BOT_EXPORT __attribute__((visibility("default")))
#else
#define FELT_BOT_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define FELT_BOT_ABI_VERSION UINT32_C(1)
#define FELT_INVALID_CARD UINT8_C(255)

typedef uint8_t FeltCard;
typedef int64_t FeltChips;

#define FELT_POSITION_BUTTON UINT32_C(0)
#define FELT_POSITION_BIG_BLIND UINT32_C(1)

#define FELT_STREET_PREFLOP UINT32_C(0)
#define FELT_STREET_FLOP UINT32_C(1)
#define FELT_STREET_TURN UINT32_C(2)
#define FELT_STREET_RIVER UINT32_C(3)

#define FELT_ACTION_FOLD UINT32_C(1)
#define FELT_ACTION_CHECK UINT32_C(2)
#define FELT_ACTION_CALL UINT32_C(3)
#define FELT_ACTION_RAISE_TO UINT32_C(4)

#define FELT_LEGAL_FOLD (UINT32_C(1) << 0)
#define FELT_LEGAL_CHECK (UINT32_C(1) << 1)
#define FELT_LEGAL_CALL (UINT32_C(1) << 2)
#define FELT_LEGAL_RAISE_TO (UINT32_C(1) << 3)

#define FELT_EVENT_POST_SMALL_BLIND UINT32_C(1)
#define FELT_EVENT_POST_BIG_BLIND UINT32_C(2)
#define FELT_EVENT_FOLD UINT32_C(3)
#define FELT_EVENT_CHECK UINT32_C(4)
#define FELT_EVENT_CALL UINT32_C(5)
#define FELT_EVENT_BET UINT32_C(6)
#define FELT_EVENT_RAISE UINT32_C(7)

typedef struct FeltAction {
  /* One FELT_ACTION_* value. Reserved must be zero. */
  uint32_t type;
  uint32_t reserved;
  /* Total contribution on this street; used only by RAISE_TO. */
  FeltChips amount_to;
} FeltAction;

typedef struct FeltActionEvent {
  /* Position and street use the FELT_POSITION_* and FELT_STREET_* codes. */
  uint32_t position;
  uint32_t street;
  /* One FELT_EVENT_* value. Reserved must be zero. */
  uint32_t type;
  uint32_t reserved;
  /* Acting player's total street contribution after this event. */
  FeltChips amount_to;
} FeltActionEvent;

typedef struct FeltGameState {
  /* Bots must reject an unsupported version or undersized structure. */
  uint32_t abi_version;
  uint32_t struct_size;

  /* Only board[0..board_count) is visible; remaining entries are 255. */
  FeltCard hole[2];
  FeltCard board[5];
  uint8_t board_count;

  uint32_t street;
  uint32_t position;
  /* Bitwise OR of FELT_LEGAL_* flags. */
  uint32_t legal_actions;
  uint32_t reserved0;

  /* Pot includes all committed chips; stacks are chips remaining behind. */
  FeltChips pot;
  FeltChips my_stack;
  FeltChips opp_stack;
  FeltChips my_street_contribution;
  FeltChips opp_street_contribution;
  /* to_call is capped by my_stack. Raise amounts are inclusive bounds. */
  FeltChips to_call;
  FeltChips min_raise_to;
  FeltChips max_raise_to;

  /* The cap is constant match configuration, not a countdown clock. */
  uint64_t decision_cap_us;
  /* The only permitted source of bot randomness. */
  uint64_t rng_seed;

  /* Read-only and valid only for the duration of felt_bot_act(). */
  const FeltActionEvent *history;
  uint32_t history_count;
  uint32_t reserved1;
} FeltGameState;

typedef uint32_t (*FeltBotAbiVersionFn)(void);
typedef const char *(*FeltBotNameFn)(void);
typedef FeltAction (*FeltBotActFn)(const FeltGameState *state);

FELT_BOT_EXPORT uint32_t felt_bot_abi_version(void);
/* Return a process-lifetime, null-terminated name containing 1 to 127 bytes. */
FELT_BOT_EXPORT const char *felt_bot_name(void);
FELT_BOT_EXPORT FeltAction felt_bot_act(const FeltGameState *state);

#ifdef __cplusplus
}
#endif

#endif

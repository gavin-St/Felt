/*
 * Felt bot template (C).
 *
 * Copy this directory, rename the bot, and put your strategy in choose_action().
 * A bot is a pure function of the state it is handed. See ../README.md for the
 * build commands and the rules that are easy to get wrong.
 */

#include "felt/bot_api.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Required exports                                                    */
/* ------------------------------------------------------------------ */

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

/* Must stay valid for the life of the process: a string literal or static
 * buffer, 1 to 127 bytes. Never a stack buffer. */
const char *felt_bot_name(void) { return "my-bot"; }

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* The action the harness substitutes when yours is illegal. Returning this
 * yourself is always safe. */
static FeltAction default_action(const FeltGameState *state) {
  FeltAction action = {0};
  action.type = (state->legal_actions & FELT_LEGAL_CHECK) != 0U
                    ? FELT_ACTION_CHECK
                    : FELT_ACTION_FOLD;
  return action;
}

static FeltAction make_action(uint32_t type, FeltChips amount_to) {
  FeltAction action = {0};
  action.type = type;
  action.amount_to = amount_to;
  return action;
}

/* Clamp a desired raise into the legal range.
 *
 * Note the short all-in case: when the only raise available is an all-in
 * smaller than a full raise, the harness reports max_raise_to < min_raise_to,
 * and the single legal amount is exactly max_raise_to. Checking the bounds in
 * the wrong order here is the most common template bug. */
static FeltChips clamp_raise_to(const FeltGameState *state, FeltChips desired) {
  if ((state->legal_actions & FELT_LEGAL_RAISE_TO) == 0U) {
    return 0;
  }
  if (state->max_raise_to < state->min_raise_to) {
    return state->max_raise_to;
  }
  if (desired < state->min_raise_to) {
    return state->min_raise_to;
  }
  if (desired > state->max_raise_to) {
    return state->max_raise_to;
  }
  return desired;
}

/* amount_to is a TOTAL street contribution, so a pot-sized raise means: call
 * first, then bet the pot that results. */
static FeltChips pot_raise_to(const FeltGameState *state) {
  return state->my_street_contribution + state->to_call +
         (state->pot + state->to_call);
}

/* All randomness must come from decision_random, so a replay of the same state
 * makes the same choice. Never use rand(), time(), or a global counter. */
static uint64_t splitmix64(uint64_t *rng) {
  uint64_t value = (*rng += UINT64_C(0x9e3779b97f4a7c15));
  value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31U);
}

/* Cards are 0-51, rank-major: rank = card >> 2 (0 = deuce, 12 = ace),
 * suit = card & 3. Board slots past board_count hold FELT_INVALID_CARD. */
static uint32_t card_rank(FeltCard card) { return (uint32_t)(card >> 2U); }
static uint32_t card_suit(FeltCard card) { return (uint32_t)(card & 3U); }

/* ------------------------------------------------------------------ */
/* Strategy                                                            */
/* ------------------------------------------------------------------ */

static FeltAction choose_action(const FeltGameState *state) {
  uint64_t rng = state->decision_random;
  (void)rng;
  (void)splitmix64;
  (void)card_rank;
  (void)card_suit;
  (void)pot_raise_to;

  /* Replace everything below with your strategy.
   *
   * Available to you: state->hole, state->board / board_count, street,
   * position (0 = button/SB, 1 = big blind), pot, my_stack, opp_stack,
   * my_street_contribution, opp_street_contribution, to_call, the raise
   * bounds, and state->history[0 .. history_count) for this hand.
   *
   * Always test legality with the mask rather than inferring it. */

  if ((state->legal_actions & FELT_LEGAL_CHECK) != 0U) {
    return make_action(FELT_ACTION_CHECK, 0);
  }
  if ((state->legal_actions & FELT_LEGAL_CALL) != 0U) {
    return make_action(FELT_ACTION_CALL, 0);
  }
  return default_action(state);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

FeltAction felt_bot_act(const FeltGameState *state) {
  /* Refuse a state this build does not understand rather than misreading it. */
  if (state == NULL || state->abi_version != FELT_BOT_ABI_VERSION ||
      state->struct_size < sizeof(FeltGameState)) {
    FeltAction action = {0};
    action.type = FELT_ACTION_FOLD;
    return action;
  }

  FeltAction action = choose_action(state);

  /* Belt and braces: never emit a raise outside the legal range. */
  if (action.type == FELT_ACTION_RAISE_TO) {
    action.amount_to = clamp_raise_to(state, action.amount_to);
    if (action.amount_to == 0) {
      return default_action(state);
    }
  } else {
    action.amount_to = 0;
  }
  return action;
}

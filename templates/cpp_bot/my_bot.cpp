/*
 * Felt bot template (C++).
 *
 * The boundary stays pure C: the three exported functions are extern "C", and
 * no exception may escape them. Inside, use whatever C++ you like.
 * See ../README.md for build commands and the rules that are easy to get wrong.
 */

#include "felt/bot_api.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

/* Anything expensive and immutable can be built once here. There is no bot
 * object and no init hook: a function-local static is initialized on first use,
 * thread-safely, and that first call pays the cost. Warm it in felt_bot_name()
 * if you would rather not pay it inside a timed decision. */
struct Tables {
  std::vector<std::uint8_t> example;
  Tables() : example(256, 0) {}
};

const Tables &tables() {
  static const Tables instance;
  return instance;
}

FeltAction make_action(std::uint32_t type, FeltChips amount_to = 0) noexcept {
  FeltAction action{};
  action.type = type;
  action.amount_to = amount_to;
  return action;
}

FeltAction default_action(const FeltGameState &state) noexcept {
  return make_action((state.legal_actions & FELT_LEGAL_CHECK) != 0U
                         ? FELT_ACTION_CHECK
                         : FELT_ACTION_FOLD);
}

/* Short all-in: when the only raise available is an all-in smaller than a full
 * raise, max_raise_to < min_raise_to and the one legal amount is exactly
 * max_raise_to. Check that before clamping to the minimum. */
FeltChips clamp_raise_to(const FeltGameState &state, FeltChips desired) noexcept {
  if ((state.legal_actions & FELT_LEGAL_RAISE_TO) == 0U) {
    return 0;
  }
  if (state.max_raise_to < state.min_raise_to) {
    return state.max_raise_to;
  }
  if (desired < state.min_raise_to) {
    return state.min_raise_to;
  }
  if (desired > state.max_raise_to) {
    return state.max_raise_to;
  }
  return desired;
}

/* amount_to is a TOTAL street contribution: call first, then bet the resulting
 * pot. */
FeltChips pot_raise_to(const FeltGameState &state) noexcept {
  return state.my_street_contribution + state.to_call +
         (state.pot + state.to_call);
}

/* All randomness must derive from decision_random so the same state always
 * produces the same choice. No <random> default engines, no clocks. */
std::uint64_t splitmix64(std::uint64_t &rng) noexcept {
  std::uint64_t value = (rng += UINT64_C(0x9e3779b97f4a7c15));
  value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31U);
}

/* Cards are 0-51, rank-major: rank = card >> 2 (0 = deuce, 12 = ace),
 * suit = card & 3. */
constexpr std::uint32_t card_rank(FeltCard card) noexcept { return card >> 2U; }
constexpr std::uint32_t card_suit(FeltCard card) noexcept { return card & 3U; }

FeltAction choose_action(const FeltGameState &state) {
  std::uint64_t rng = state.decision_random;
  (void)rng;
  (void)splitmix64;
  (void)tables();
  (void)pot_raise_to(state);
  (void)card_rank;
  (void)card_suit;

  /* Replace everything below with your strategy. Test legality with the mask
   * rather than inferring it from to_call or the street. */

  if ((state.legal_actions & FELT_LEGAL_CHECK) != 0U) {
    return make_action(FELT_ACTION_CHECK);
  }
  if ((state.legal_actions & FELT_LEGAL_CALL) != 0U) {
    return make_action(FELT_ACTION_CALL);
  }
  return default_action(state);
}

}  // namespace

extern "C" {

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char *felt_bot_name(void) { return "my-cpp-bot"; }

FeltAction felt_bot_act(const FeltGameState *state) {
  if (state == nullptr || state->abi_version != FELT_BOT_ABI_VERSION ||
      state->struct_size < sizeof(FeltGameState)) {
    return make_action(FELT_ACTION_FOLD);
  }

  /* An exception crossing this boundary is undefined behaviour and will take
   * the match worker down. Catch everything. */
  try {
    FeltAction action = choose_action(*state);
    if (action.type == FELT_ACTION_RAISE_TO) {
      action.amount_to = clamp_raise_to(*state, action.amount_to);
      if (action.amount_to == 0) {
        return default_action(*state);
      }
    } else {
      action.amount_to = 0;
    }
    return action;
  } catch (...) {
    return default_action(*state);
  }
}

}  // extern "C"

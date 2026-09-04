#include "felt/bot_api.h"
#include "felt/native_bot_runner.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

constexpr std::uint32_t rank_of(FeltCard card) { return card >> 2U; }
constexpr std::uint32_t suit_of(FeltCard card) { return card & 3U; }

bool is_nit_hand(FeltCard first, FeltCard second) {
  return rank_of(first) == rank_of(second) && rank_of(first) >= 10U;
}

bool is_better_hand(FeltCard first, FeltCard second) {
  const std::uint32_t first_rank = rank_of(first);
  const std::uint32_t second_rank = rank_of(second);
  if (first_rank == second_rank) {
    return first_rank >= 7U;
  }
  return (first_rank >= 8U && second_rank >= 8U) || first_rank >= 11U ||
         second_rank >= 11U;
}

bool is_worse_hand(FeltCard first, FeltCard second) {
  const std::uint32_t first_rank = rank_of(first);
  const std::uint32_t second_rank = rank_of(second);
  const std::uint32_t high = std::max(first_rank, second_rank);
  const std::uint32_t low = std::min(first_rank, second_rank);
  return first_rank != second_rank && suit_of(first) != suit_of(second) &&
         high <= 10U && high - low >= 3U;
}

bool is_solved_open(FeltCard first, FeltCard second) {
  const std::uint32_t first_rank = rank_of(first);
  const std::uint32_t second_rank = rank_of(second);
  if (first_rank == second_rank) {
    return first_rank >= 10U;
  }
  const std::uint32_t other = first_rank == 12U ? second_rank : first_rank;
  if (first_rank != 12U && second_rank != 12U) {
    return false;
  }
  if (other == 11U) {
    return true;
  }
  return suit_of(first) == suit_of(second) &&
         (other == 10U || other == 9U || other == 8U || other == 3U ||
          other == 2U);
}

bool is_solved_small_response(FeltCard first, FeltCard second) {
  return rank_of(first) == rank_of(second) && rank_of(first) >= 11U;
}

bool is_solved_large_response(FeltCard first, FeltCard second) {
  const std::uint32_t first_rank = rank_of(first);
  const std::uint32_t second_rank = rank_of(second);
  return (first_rank == second_rank && first_rank >= 10U) ||
         (suit_of(first) == suit_of(second) &&
          ((first_rank == 12U && second_rank == 11U) ||
           (first_rank == 11U && second_rank == 12U)));
}

FeltGameState base_state(FeltCard first, FeltCard second) {
  FeltGameState state{};
  state.abi_version = FELT_BOT_ABI_VERSION;
  state.struct_size = sizeof(FeltGameState);
  state.hole[0] = first;
  state.hole[1] = second;
  std::fill(std::begin(state.board), std::end(state.board), FELT_INVALID_CARD);
  state.street = FELT_STREET_PREFLOP;
  state.position = FELT_POSITION_BUTTON;
  state.legal_actions =
      FELT_LEGAL_FOLD | FELT_LEGAL_CALL | FELT_LEGAL_RAISE_TO;
  state.pot = 150;
  state.my_stack = 19'950;
  state.opp_stack = 19'900;
  state.my_street_contribution = 50;
  state.opp_street_contribution = 100;
  state.to_call = 50;
  state.min_raise_to = 200;
  state.max_raise_to = 20'000;
  state.decision_cap_us = 2'000;
  return state;
}

void require_strategy_action(const FeltAction& action,
                             bool selected,
                             bool can_check,
                             bool can_raise,
                             FeltChips maximum,
                             const std::string& context) {
  if (selected) {
    const std::uint32_t expected = can_raise ? FELT_ACTION_RAISE_TO
                                             : FELT_ACTION_CALL;
    if (action.type != expected ||
        (can_raise && action.amount_to != maximum)) {
      throw std::runtime_error(context + " did not shove/call as expected");
    }
    return;
  }
  const std::uint32_t expected = can_check ? FELT_ACTION_CHECK : FELT_ACTION_FOLD;
  if (action.type != expected) {
    throw std::runtime_error(context + " did not check/fold as expected");
  }
}

void set_big_blind_history(FeltGameState& state,
                           std::array<FeltActionEvent, 2>& history) {
  history = {{{FELT_POSITION_BUTTON, FELT_STREET_PREFLOP,
               FELT_EVENT_POST_SMALL_BLIND, 0, 50},
              {FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP,
               FELT_EVENT_POST_BIG_BLIND, 0, 100}}};
  state.history = history.data();
  state.history_count = static_cast<std::uint32_t>(history.size());
}

void test_simple_ranges(felt::NativeBotRunner& nit,
                        felt::NativeBotRunner& better,
                        felt::NativeBotRunner& worse) {
  unsigned nit_count = 0;
  unsigned better_count = 0;
  unsigned worse_count = 0;
  for (FeltCard first = 0; first < 52; ++first) {
    for (FeltCard second = first + 1; second < 52; ++second) {
      const FeltGameState state = base_state(first, second);
      const bool nit_selected = is_nit_hand(first, second);
      const bool better_selected = is_better_hand(first, second);
      const bool worse_selected = is_worse_hand(first, second);
      nit_count += nit_selected;
      better_count += better_selected;
      worse_count += worse_selected;
      require_strategy_action(nit.act(state), nit_selected, false, true,
                              state.max_raise_to, "nit-all-in");
      require_strategy_action(better.act(state), better_selected, false, true,
                              state.max_raise_to, "better-all-in");
      require_strategy_action(worse.act(state), worse_selected, false, true,
                              state.max_raise_to, "worse-all-in");
    }
  }
  if (nit_count != 18U || better_count != 452U || worse_count != 432U) {
    throw std::runtime_error("simple bot range combination count changed");
  }
}

void test_solved_ranges(felt::NativeBotRunner& solved) {
  unsigned open_count = 0;
  unsigned small_count = 0;
  unsigned large_count = 0;
  for (FeltCard first = 0; first < 52; ++first) {
    for (FeltCard second = first + 1; second < 52; ++second) {
      FeltGameState state = base_state(first, second);
      std::array<FeltActionEvent, 2> history{};
      set_big_blind_history(state, history);

      const bool open = is_solved_open(first, second);
      const bool small = is_solved_small_response(first, second);
      const bool large = is_solved_large_response(first, second);
      open_count += open;
      small_count += small;
      large_count += large;

      require_strategy_action(solved.act(state), open, false, true,
                              state.max_raise_to, "solved SB open");

      state.position = FELT_POSITION_BIG_BLIND;
      state.pot = 200;
      state.my_stack = 19'900;
      state.opp_stack = 19'900;
      state.my_street_contribution = 100;
      state.opp_street_contribution = 100;
      state.to_call = 0;
      state.legal_actions = FELT_LEGAL_CHECK | FELT_LEGAL_RAISE_TO;
      require_strategy_action(solved.act(state), small, true, true,
                              state.max_raise_to, "solved limp response");

      state.pot = 7'500;
      state.my_stack = 19'900;
      state.opp_stack = 12'600;
      state.my_street_contribution = 100;
      state.opp_street_contribution = 7'400;
      state.to_call = 7'300;
      state.legal_actions =
          FELT_LEGAL_FOLD | FELT_LEGAL_CALL | FELT_LEGAL_RAISE_TO;
      require_strategy_action(solved.act(state), small, false, true,
                              state.max_raise_to, "solved 74 bb response");

      state.pot = 7'600;
      state.opp_stack = 12'500;
      state.opp_street_contribution = 7'500;
      state.to_call = 7'400;
      require_strategy_action(solved.act(state), large, false, true,
                              state.max_raise_to, "solved 75 bb response");

      state.pot = 20'100;
      state.opp_stack = 0;
      state.opp_street_contribution = 20'000;
      state.to_call = 19'900;
      state.legal_actions = FELT_LEGAL_FOLD | FELT_LEGAL_CALL;
      state.min_raise_to = 0;
      state.max_raise_to = 0;
      require_strategy_action(solved.act(state), large, false, false, 0,
                              "solved all-in response");
    }
  }
  if (open_count != 54U || small_count != 12U || large_count != 22U) {
    throw std::runtime_error("solved bot range combination count changed");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "expected nit, better, worse, and solved bot library paths\n";
    return 2;
  }
  try {
    felt::NativeBotRunner nit(argv[1]);
    felt::NativeBotRunner better(argv[2]);
    felt::NativeBotRunner worse(argv[3]);
    felt::NativeBotRunner solved(argv[4]);
    test_simple_ranges(nit, better, worse);
    test_solved_ranges(solved);
  } catch (const std::exception& error) {
    std::cerr << "reference_bot_strategy_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

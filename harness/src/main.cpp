#include "felt/bot_api.h"
#include "felt/native_bot_runner.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <iterator>

namespace {

FeltGameState make_probe_state() {
  FeltGameState state{};
  state.abi_version = FELT_BOT_ABI_VERSION;
  state.struct_size = sizeof(FeltGameState);
  state.hole[0] = 0;
  state.hole[1] = 1;
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
  state.rng_seed = UINT64_C(0x5eed);
  return state;
}

void print_probe(felt::BotRunner& bot, const FeltGameState& state) {
  const FeltAction action = bot.act(state);
  std::cout << bot.name() << ": action=" << action.type
            << " amount_to=" << action.amount_to << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: run_match BOT_A.dylib BOT_B.dylib\n";
    return 2;
  }

  try {
    felt::NativeBotRunner bot_a(argv[1]);
    felt::NativeBotRunner bot_b(argv[2]);
    const FeltGameState state = make_probe_state();

    std::cout << "Felt M0 bot probe\n";
    print_probe(bot_a, state);
    print_probe(bot_b, state);
  } catch (const std::exception& error) {
    std::cerr << "run_match: " << error.what() << '\n';
    return 1;
  }

  return 0;
}

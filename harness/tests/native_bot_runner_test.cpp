#include "felt/bot_api.h"
#include "felt/native_bot_runner.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

FeltGameState facing_bet_state() {
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
  state.decision_random = UINT64_C(0x123456789abcdef0);
  return state;
}

void expect_load_failure(const char* path, const std::string& expected_text) {
  try {
    felt::NativeBotRunner bot(path);
    (void)bot;
  } catch (const std::exception& error) {
    require(std::string(error.what()).find(expected_text) != std::string::npos,
            "load failure did not mention '" + expected_text + "': " +
                error.what());
    return;
  }
  throw std::runtime_error("expected bot load failure for " + std::string(path));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 7) {
    std::cerr << "expected six bot library paths\n";
    return 2;
  }

  try {
    felt::NativeBotRunner check_fold(argv[1]);
    felt::NativeBotRunner check_call(argv[2]);
    felt::NativeBotRunner always_all_in(argv[3]);
    felt::NativeBotRunner seeded_random(argv[4]);

    require(check_fold.name() == "check-fold", "wrong check-fold name");
    require(check_call.name() == "check-call", "wrong check-call name");
    require(always_all_in.name() == "always-all-in",
            "wrong always-all-in name");
    require(seeded_random.name() == "seeded-random",
            "wrong seeded-random name");

    const FeltGameState facing_bet = facing_bet_state();
    require(check_fold.act(facing_bet).type == FELT_ACTION_FOLD,
            "check-fold did not fold");
    require(check_call.act(facing_bet).type == FELT_ACTION_CALL,
            "check-call did not call");

    const FeltAction all_in_action = always_all_in.act(facing_bet);
    require(all_in_action.type == FELT_ACTION_RAISE_TO,
            "always-all-in did not raise");
    require(all_in_action.amount_to == facing_bet.max_raise_to,
            "always-all-in did not raise to the maximum");

    const FeltAction random_first = seeded_random.act(facing_bet);
    const FeltAction random_second = seeded_random.act(facing_bet);
    require(random_first.type == random_second.type &&
                random_first.amount_to == random_second.amount_to,
            "seeded-random was not deterministic");

    FeltGameState can_check = facing_bet;
    can_check.legal_actions = FELT_LEGAL_CHECK | FELT_LEGAL_RAISE_TO;
    can_check.to_call = 0;
    can_check.my_street_contribution = 0;
    can_check.opp_street_contribution = 0;
    can_check.min_raise_to = 100;
    require(check_fold.act(can_check).type == FELT_ACTION_CHECK,
            "check-fold did not check when possible");
    require(check_call.act(can_check).type == FELT_ACTION_CHECK,
            "check-call did not check when possible");
    const FeltAction open_shove = always_all_in.act(can_check);
    require(open_shove.type == FELT_ACTION_RAISE_TO &&
                open_shove.amount_to == can_check.max_raise_to,
            "always-all-in did not open-shove");

    FeltGameState cannot_reraise = facing_bet;
    cannot_reraise.legal_actions = FELT_LEGAL_FOLD | FELT_LEGAL_CALL;
    cannot_reraise.min_raise_to = 0;
    cannot_reraise.max_raise_to = 0;
    require(always_all_in.act(cannot_reraise).type == FELT_ACTION_CALL,
            "always-all-in did not call when raising was unavailable");

    felt::NativeBotRunner second_copy(argv[1]);
    require(second_copy.name() == check_fold.name(),
            "loading the same library twice changed its name");

    expect_load_failure(argv[5], "ABI version");
    expect_load_failure(argv[6], "felt_bot_act");
    expect_load_failure("/path/that/does/not/exist/felt-bot.dylib",
                        "cannot load bot");
  } catch (const std::exception& error) {
    std::cerr << "native_bot_runner_test: " << error.what() << '\n';
    return 1;
  }

  return 0;
}

#include "felt/bot_loader.hpp"
#include "felt/wasm_bot_runner.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

FeltGameState state_with_history(const FeltActionEvent& event) {
  FeltGameState state{};
  state.abi_version = FELT_BOT_ABI_VERSION;
  state.struct_size = sizeof(state);
  state.hole[0] = 48;
  state.hole[1] = 1;
  state.board[0] = 5;
  state.board[1] = 9;
  state.board[2] = 17;
  state.board[3] = FELT_INVALID_CARD;
  state.board[4] = FELT_INVALID_CARD;
  state.board_count = 3;
  state.street = FELT_STREET_FLOP;
  state.position = FELT_POSITION_BIG_BLIND;
  state.legal_actions = FELT_LEGAL_FOLD | FELT_LEGAL_CALL;
  state.pot = 321;
  state.to_call = 12;
  state.decision_cap_us = 2'000;
  state.decision_random = 77;
  state.history = &event;
  state.history_count = 1;
  return state;
}

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: wasm_bot_runner_test CHECK_CALL.wasm HANGING.wasm "
                 "IMPORTING.wasm\n";
    return 2;
  }

  try {
    require(felt::wasm_bot_runtime_available(), "Wasm runtime is unavailable");
    std::unique_ptr<felt::BotRunner> bot = felt::load_bot_runner(argv[1]);
    require(bot->name() == "wasm-check-call-test", "wrong Wasm bot name");

    FeltActionEvent event{};
    event.position = FELT_POSITION_BUTTON;
    event.street = FELT_STREET_FLOP;
    event.type = FELT_EVENT_BET;
    event.amount_to = 123;
    const FeltGameState state = state_with_history(event);
    require(bot->act(state).type == FELT_ACTION_CALL,
            "state or history did not cross the Wasm boundary correctly");

    felt::WasmBotRunner hanging(argv[2]);
    bool trapped = false;
    try {
      (void)hanging.act(state);
    } catch (const std::exception& error) {
      trapped = std::string(error.what()).find("fuel") != std::string::npos;
    }
    require(trapped, "infinite Wasm bot did not exhaust fuel");

    bool rejected_import = false;
    try {
      felt::WasmBotRunner importing(argv[3]);
    } catch (const std::exception& error) {
      rejected_import =
          std::string(error.what()).find("imports host functions") !=
          std::string::npos;
    }
    require(rejected_import, "Wasm bot with a host import was not rejected");
  } catch (const std::exception& error) {
    std::cerr << "wasm_bot_runner_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

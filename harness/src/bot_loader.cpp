#include "felt/bot_loader.hpp"

#include "felt/native_bot_runner.hpp"
#include "felt/wasm_bot_runner.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace felt {

std::unique_ptr<BotRunner> load_bot_runner(std::string path) {
  if (std::filesystem::path(path).extension() == ".wasm") {
    return std::make_unique<WasmBotRunner>(std::move(path));
  }
  return std::make_unique<NativeBotRunner>(std::move(path));
}

}  // namespace felt

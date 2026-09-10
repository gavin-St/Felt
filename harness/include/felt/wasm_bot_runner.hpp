#ifndef FELT_WASM_BOT_RUNNER_HPP
#define FELT_WASM_BOT_RUNNER_HPP

#include "felt/bot_runner.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace felt {

class WasmBotRunner final : public BotRunner {
 public:
  explicit WasmBotRunner(std::string path);
  ~WasmBotRunner() override;

  WasmBotRunner(const WasmBotRunner&) = delete;
  WasmBotRunner& operator=(const WasmBotRunner&) = delete;
  WasmBotRunner(WasmBotRunner&&) = delete;
  WasmBotRunner& operator=(WasmBotRunner&&) = delete;

  [[nodiscard]] std::string_view name() const noexcept override;
  [[nodiscard]] FeltAction act(const FeltGameState& state) override;
  [[nodiscard]] const std::string& path() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string path_;
  std::string name_;
};

[[nodiscard]] bool wasm_bot_runtime_available() noexcept;

}  // namespace felt

#endif

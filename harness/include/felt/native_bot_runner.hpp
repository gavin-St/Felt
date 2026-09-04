#ifndef FELT_NATIVE_BOT_RUNNER_HPP
#define FELT_NATIVE_BOT_RUNNER_HPP

#include "felt/bot_runner.hpp"

#include <string>
#include <string_view>

namespace felt {

class NativeBotRunner final : public BotRunner {
 public:
  explicit NativeBotRunner(std::string path);
  ~NativeBotRunner() override;

  NativeBotRunner(const NativeBotRunner&) = delete;
  NativeBotRunner& operator=(const NativeBotRunner&) = delete;
  NativeBotRunner(NativeBotRunner&&) = delete;
  NativeBotRunner& operator=(NativeBotRunner&&) = delete;

  [[nodiscard]] std::string_view name() const noexcept override;
  [[nodiscard]] FeltAction act(const FeltGameState& state) override;
  [[nodiscard]] const std::string& path() const noexcept;

 private:
  void* handle_{nullptr};
  std::string path_;
  std::string name_;
  FeltBotActFn act_{nullptr};
};

}  // namespace felt

#endif

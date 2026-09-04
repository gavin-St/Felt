#ifndef FELT_BOT_RUNNER_HPP
#define FELT_BOT_RUNNER_HPP

#include "felt/bot_api.h"

#include <string_view>

namespace felt {

class BotRunner {
 public:
  virtual ~BotRunner() = default;

  [[nodiscard]] virtual std::string_view name() const noexcept = 0;
  [[nodiscard]] virtual FeltAction act(const FeltGameState& state) = 0;
};

}  // namespace felt

#endif

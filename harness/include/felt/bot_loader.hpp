#ifndef FELT_BOT_LOADER_HPP
#define FELT_BOT_LOADER_HPP

#include "felt/bot_runner.hpp"

#include <memory>
#include <string>

namespace felt {

[[nodiscard]] std::unique_ptr<BotRunner> load_bot_runner(std::string path);

}  // namespace felt

#endif

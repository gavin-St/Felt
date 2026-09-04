#ifndef FELT_MATCH_CLI_HPP
#define FELT_MATCH_CLI_HPP

#include "felt/match.hpp"

#include <array>
#include <string>

namespace felt {

struct MatchCliOptions {
  std::array<std::string, 2> bot_paths;
  MatchConfig match;
  bool seed_provided{false};
};

[[nodiscard]] MatchCliOptions parse_match_cli(int argc,
                                              const char* const argv[]);
[[nodiscard]] const char* match_usage() noexcept;

}  // namespace felt

#endif

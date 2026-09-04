#include "felt/match_cli.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace felt {
namespace {

std::uint64_t parse_u64(std::string_view text, std::string_view option) {
  std::uint64_t value = 0;
  const char* const begin = text.data();
  const char* const end = text.data() + text.size();
  const auto [next, error] = std::from_chars(begin, end, value);
  if (text.empty() || error != std::errc{} || next != end) {
    throw std::invalid_argument(std::string(option) +
                                " requires an unsigned integer");
  }
  return value;
}

FeltChips parse_chips(std::string_view text, std::string_view option) {
  const std::uint64_t value = parse_u64(text, option);
  if (value > static_cast<std::uint64_t>(
                  std::numeric_limits<FeltChips>::max())) {
    throw std::invalid_argument(std::string(option) + " is too large");
  }
  return static_cast<FeltChips>(value);
}

std::string_view require_value(int argc,
                               const char* const argv[],
                               int& index,
                               std::string_view option) {
  if (index + 1 >= argc) {
    throw std::invalid_argument(std::string(option) + " requires a value");
  }
  ++index;
  return argv[index];
}

}  // namespace

const char* match_usage() noexcept {
  return "usage: run_match BOT_A.dylib BOT_B.dylib [--hands N] [--seed N] "
         "[--stack CHIPS] [--sb CHIPS] [--bb CHIPS] "
         "[--decision-cap-ms N] [--no-duplicate] [--out DIRECTORY]";
}

MatchCliOptions parse_match_cli(int argc, const char* const argv[]) {
  if (argc < 3) {
    throw std::invalid_argument(match_usage());
  }

  MatchCliOptions options;
  options.bot_paths = {argv[1], argv[2]};
  for (int index = 3; index < argc; ++index) {
    const std::string_view option(argv[index]);
    if (option == "--no-duplicate") {
      options.match.duplicate = false;
    } else if (option == "--out") {
      options.output_directory =
          std::string(require_value(argc, argv, index, option));
      if (options.output_directory.empty()) {
        throw std::invalid_argument("--out requires a nonempty directory");
      }
    } else if (option == "--hands") {
      options.match.hand_count =
          parse_u64(require_value(argc, argv, index, option), option);
    } else if (option == "--seed") {
      options.match.match_seed =
          parse_u64(require_value(argc, argv, index, option), option);
      options.seed_provided = true;
    } else if (option == "--stack") {
      options.match.starting_stack =
          parse_chips(require_value(argc, argv, index, option), option);
    } else if (option == "--sb") {
      options.match.small_blind =
          parse_chips(require_value(argc, argv, index, option), option);
    } else if (option == "--bb") {
      options.match.big_blind =
          parse_chips(require_value(argc, argv, index, option), option);
    } else if (option == "--decision-cap-ms") {
      const std::uint64_t milliseconds =
          parse_u64(require_value(argc, argv, index, option), option);
      if (milliseconds == 0 ||
          milliseconds > std::numeric_limits<std::uint64_t>::max() / 1'000U) {
        throw std::invalid_argument(
            "--decision-cap-ms must be a positive representable value");
      }
      options.match.decision_cap_us = milliseconds * 1'000U;
    } else {
      throw std::invalid_argument("unknown option: " + std::string(option));
    }
  }

  validate_match_config(options.match);
  return options;
}

}  // namespace felt

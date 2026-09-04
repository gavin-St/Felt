#include "felt/match_cli.hpp"

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <std::size_t Size>
felt::MatchCliOptions parse(const char* const (&arguments)[Size]) {
  return felt::parse_match_cli(static_cast<int>(Size), arguments);
}

void require_invalid(const std::function<void()>& operation,
                     const std::string& message) {
  try {
    operation();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

void test_defaults() {
  const char* const arguments[]{"run_match", "a.dylib", "b.dylib"};
  const felt::MatchCliOptions options = parse(arguments);
  require(options.bot_paths[0] == "a.dylib" &&
              options.bot_paths[1] == "b.dylib",
          "bot paths were parsed incorrectly");
  require(options.match.hand_count == 20'000 && options.match.duplicate &&
              options.match.starting_stack == 20'000 &&
              options.match.small_blind == 50 &&
              options.match.big_blind == 100 &&
              options.match.decision_cap_us == 2'000 &&
              options.match.equity_adjustment &&
              options.output_directory == "results" &&
              !options.seed_provided,
          "CLI defaults were wrong");
}

void test_overrides() {
  const char* const arguments[]{
      "run_match",        "a",       "b",       "--hands",
      "3",                "--seed",  "0",       "--stack",
      "1000",             "--sb",    "5",       "--bb",
      "10",               "--decision-cap-ms", "500",     "--out",
      "custom-results",   "--no-duplicate", "--no-equity-adjust"};
  const felt::MatchCliOptions options = parse(arguments);
  require(options.match.hand_count == 3 && !options.match.duplicate &&
              options.match.match_seed == 0 && options.seed_provided &&
              options.match.starting_stack == 1'000 &&
              options.match.small_blind == 5 &&
              options.match.big_blind == 10 &&
              options.match.decision_cap_us == 500'000 &&
              !options.match.equity_adjustment,
          "CLI overrides were wrong");
  require(options.output_directory == "custom-results",
          "output directory override was wrong");
}

void test_rejections() {
  const char* const too_few[]{"run_match", "a"};
  require_invalid([&] { (void)parse(too_few); },
                  "missing bot path was accepted");

  const char* const odd_duplicate[]{"run_match", "a", "b", "--hands",
                                    "3"};
  require_invalid([&] { (void)parse(odd_duplicate); },
                  "odd duplicate hand count was accepted by CLI");

  const char* const bad_blinds[]{"run_match", "a", "b", "--stack", "99"};
  require_invalid([&] { (void)parse(bad_blinds); },
                  "invalid chip configuration was accepted by CLI");

  const char* const negative[]{"run_match", "a", "b", "--hands", "-2"};
  require_invalid([&] { (void)parse(negative); },
                  "negative integer was accepted by CLI");

  const char* const missing_value[]{"run_match", "a", "b", "--seed"};
  require_invalid([&] { (void)parse(missing_value); },
                  "missing option value was accepted by CLI");

  const char* const zero_cap[]{"run_match", "a", "b", "--decision-cap-ms",
                               "0"};
  require_invalid([&] { (void)parse(zero_cap); },
                  "zero decision cap was accepted by CLI");

  const char* const unknown[]{"run_match", "a", "b", "--wat"};
  require_invalid([&] { (void)parse(unknown); },
                  "unknown option was accepted by CLI");

  const char* const empty_out[]{"run_match", "a", "b", "--out", ""};
  require_invalid([&] { (void)parse(empty_out); },
                  "empty output directory was accepted by CLI");
}

}  // namespace

int main() {
  try {
    test_defaults();
    test_overrides();
    test_rejections();
  } catch (const std::exception& error) {
    std::cerr << "match_cli_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

#include "felt/match.hpp"
#include "felt/match_cli.hpp"
#include "felt/match_log.hpp"
#include "felt/native_bot_runner.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::uint64_t random_seed() {
  std::uint64_t seed = 0;
  arc4random_buf(&seed, sizeof(seed));
  return seed;
}

void print_bot_result(std::string_view name,
                      std::size_t bot_index,
                      const felt::MatchResult& result) {
  std::cout << name << ": adjusted_net_chips="
            << result.adjusted_net_by_bot[bot_index]
            << " raw_net_chips=" << result.raw_net_by_bot[bot_index]
            << " adjusted_button="
            << result.adjusted_net_by_bot_and_position[bot_index]
                                                            [FELT_POSITION_BUTTON]
            << " adjusted_big_blind="
            << result.adjusted_net_by_bot_and_position[bot_index]
                                                            [FELT_POSITION_BIG_BLIND]
            << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    std::cout << felt::match_usage() << '\n';
    return 0;
  }

  try {
    felt::MatchCliOptions options = felt::parse_match_cli(argc, argv);
    if (!options.seed_provided) {
      options.match.match_seed = random_seed();
    }

    felt::NativeBotRunner bot_a(options.bot_paths[0]);
    felt::NativeBotRunner bot_b(options.bot_paths[1]);
    std::array<felt::BotArtifact, 2> artifacts{
        felt::inspect_bot_artifact(options.bot_paths[0],
                                   std::string(bot_a.name())),
        felt::inspect_bot_artifact(options.bot_paths[1],
                                   std::string(bot_b.name()))};
    felt::MatchLogWriter log(options.output_directory, options.match,
                             std::move(artifacts));
    const felt::MatchResult result =
        felt::play_match(options.match, bot_a, bot_b, &log);
    log.finish(result);

    std::cout << "Felt match complete\n"
              << "seed=" << options.match.match_seed << '\n'
              << "hands=" << result.hand_count
              << " duplicate=" << (options.match.duplicate ? "true" : "false")
              << " equity_adjustment="
              << (options.match.equity_adjustment ? "true" : "false")
              << '\n'
              << "output=" << options.output_directory << '\n';
    print_bot_result(bot_a.name(), 0, result);
    print_bot_result(bot_b.name(), 1, result);
    std::cout << "adjusted payouts are the headline result; raw runout payouts "
                 "are retained for inspection\n";
  } catch (const std::exception& error) {
    std::cerr << "run_match: " << error.what() << '\n';
    return 1;
  }

  return 0;
}

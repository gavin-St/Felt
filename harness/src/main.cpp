#include "felt/match_cli.hpp"
#include "felt/match_process.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

namespace {

std::uint64_t random_seed() {
  std::uint64_t seed = 0;
  arc4random_buf(&seed, sizeof(seed));
  return seed;
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
    return felt::run_supervised_match(options);
  } catch (const std::exception& error) {
    std::cerr << "run_match: " << error.what() << '\n';
    return 1;
  }
}

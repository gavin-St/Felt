#include "felt/bot_loader.hpp"
#include "felt/match_log.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr <<
        "usage: rerun_match OUTPUT_DIRECTORY BOT_A BOT_B\n";
    return 2;
  }
  try {
    std::unique_ptr<felt::BotRunner> bot_a = felt::load_bot_runner(argv[2]);
    std::unique_ptr<felt::BotRunner> bot_b = felt::load_bot_runner(argv[3]);
    const felt::RerunReport report =
        felt::rerun_match_log(argv[1], *bot_a, *bot_b);
    std::cout << "compared " << report.hands_compared
              << " hands: hands_different=" << report.hands_different
              << " decisions_different=" << report.decisions_different
              << '\n';
    return report.hands_different == 0 ? 0 : 3;
  } catch (const std::exception& error) {
    std::cerr << "rerun_match: " << error.what() << '\n';
    return 1;
  }
}

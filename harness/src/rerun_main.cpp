#include "felt/match_log.hpp"
#include "felt/native_bot_runner.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr <<
        "usage: rerun_match OUTPUT_DIRECTORY BOT_A.dylib BOT_B.dylib\n";
    return 2;
  }
  try {
    felt::NativeBotRunner bot_a(argv[2]);
    felt::NativeBotRunner bot_b(argv[3]);
    const felt::RerunReport report =
        felt::rerun_match_log(argv[1], bot_a, bot_b);
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

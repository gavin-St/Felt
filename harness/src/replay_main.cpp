#include "felt/match_log.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: replay_match OUTPUT_DIRECTORY\n";
    return 2;
  }
  try {
    const felt::ReplayReport report = felt::replay_match_log(argv[1]);
    std::cout << "verified " << report.hands_verified
              << " logged hands\n";
  } catch (const std::exception& error) {
    std::cerr << "replay_match: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

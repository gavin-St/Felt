#ifndef FELT_MATCH_LOG_HPP
#define FELT_MATCH_LOG_HPP

#include "felt/match.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <string>

namespace felt {

struct BotArtifact {
  std::string name;
  std::string path;
  std::string sha256;
};

[[nodiscard]] BotArtifact inspect_bot_artifact(const std::string& path,
                                               std::string name);

class MatchLogWriter final : public MatchObserver {
 public:
  MatchLogWriter(std::string output_directory,
                 MatchConfig config,
                 std::array<BotArtifact, 2> bots);
  ~MatchLogWriter() override;

  MatchLogWriter(const MatchLogWriter&) = delete;
  MatchLogWriter& operator=(const MatchLogWriter&) = delete;

  void on_hand(const MatchHand& hand) override;
  void finish(const MatchResult& result);

 private:
  void write_summary(const MatchResult* result);

  std::string output_directory_;
  MatchConfig config_;
  std::array<BotArtifact, 2> bots_;
  std::ofstream hands_;
  std::uint64_t hands_written_{};
  bool finished_{false};
};

struct ReplayReport {
  std::uint64_t hands_verified{};
};

struct RerunReport {
  std::uint64_t hands_compared{};
  std::uint64_t hands_different{};
  std::uint64_t decisions_different{};
};

[[nodiscard]] ReplayReport replay_match_log(
    const std::string& output_directory);
[[nodiscard]] RerunReport rerun_match_log(
    const std::string& output_directory,
    BotRunner& bot_a,
    BotRunner& bot_b);

}  // namespace felt

#endif

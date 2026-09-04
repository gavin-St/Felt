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

struct MatchAbortInfo {
  std::string reason;
  std::uint64_t completed_hands{};
  bool has_active_decision{};
  std::uint64_t hand_index{};
  std::uint64_t decision_index{};
  std::uint32_t bot_index{};
  std::uint32_t position{};
  std::uint32_t street{};
  std::uint64_t hard_timeout_ms{};
  int worker_status{};
};

[[nodiscard]] BotArtifact inspect_bot_artifact(const std::string& path,
                                               std::string name);
void mark_match_log_aborted(const std::string& output_directory,
                            const MatchAbortInfo& abort);

class MatchLogWriter final : public MatchObserver {
 public:
  MatchLogWriter(std::string output_directory,
                 MatchConfig config,
                 std::array<BotArtifact, 2> bots);
  ~MatchLogWriter() override;

  MatchLogWriter(const MatchLogWriter&) = delete;
  MatchLogWriter& operator=(const MatchLogWriter&) = delete;

  void on_hand(const MatchHand& hand) override;
  void flush();
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

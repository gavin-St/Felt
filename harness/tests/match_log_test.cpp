#include "felt/match_log.hpp"

#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class CheckFoldBot final : public felt::BotRunner {
 public:
  std::string_view name() const noexcept override { return "check-fold"; }
  FeltAction act(const FeltGameState& state) override {
    return FeltAction{(state.legal_actions & FELT_LEGAL_CHECK) != 0U
                          ? FELT_ACTION_CHECK
                          : FELT_ACTION_FOLD,
                      0U, 0};
  }
};

class AlwaysAllInBot final : public felt::BotRunner {
 public:
  std::string_view name() const noexcept override { return "always-all-in"; }
  FeltAction act(const FeltGameState& state) override {
    if ((state.legal_actions & FELT_LEGAL_RAISE_TO) != 0U) {
      return FeltAction{FELT_ACTION_RAISE_TO, 0U, state.max_raise_to};
    }
    if ((state.legal_actions & FELT_LEGAL_CALL) != 0U) {
      return FeltAction{FELT_ACTION_CALL, 0U, 0};
    }
    if ((state.legal_actions & FELT_LEGAL_CHECK) != 0U) {
      return FeltAction{FELT_ACTION_CHECK, 0U, 0};
    }
    return FeltAction{FELT_ACTION_FOLD, 0U, 0};
  }
};

class IllegalBot final : public felt::BotRunner {
 public:
  std::string_view name() const noexcept override { return "illegal"; }
  FeltAction act(const FeltGameState&) override {
    return FeltAction{999U, 0U, 0};
  }
};

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  require(static_cast<bool>(input), "could not read generated log file");
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

void write_file(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::out | std::ios::trunc);
  require(static_cast<bool>(output), "could not write test log file");
  output << text;
  require(static_cast<bool>(output), "failed to write test log file");
}

void create_log(const std::string& directory, const std::string& artifact_path) {
  AlwaysAllInBot bot_a;
  CheckFoldBot bot_b;
  felt::MatchConfig config;
  config.hand_count = 130;
  config.match_seed = 17;
  config.equity_adjustment = false;
  std::array<felt::BotArtifact, 2> artifacts{
      felt::inspect_bot_artifact(artifact_path, std::string(bot_a.name())),
      felt::inspect_bot_artifact(artifact_path, std::string(bot_b.name()))};
  felt::MatchLogWriter writer(directory, config, std::move(artifacts));
  const felt::MatchResult result =
      felt::play_match(config, bot_a, bot_b, &writer);
  writer.finish(result);
}

void test_adjusted_log_replay(const std::string& directory,
                              const std::string& artifact_path) {
  AlwaysAllInBot bot_a;
  AlwaysAllInBot bot_b;
  felt::MatchConfig config;
  config.hand_count = 2;
  config.match_seed = 321;
  std::array<felt::BotArtifact, 2> artifacts{
      felt::inspect_bot_artifact(artifact_path, std::string(bot_a.name())),
      felt::inspect_bot_artifact(artifact_path, std::string(bot_b.name()))};
  felt::MatchLogWriter writer(directory, config, std::move(artifacts));
  const felt::MatchResult result =
      felt::play_match(config, bot_a, bot_b, &writer);
  writer.finish(result);

  const std::string hands =
      read_file(std::filesystem::path(directory) / "hands.jsonl");
  require(hands.find("\"equity\":{\"boards\":1712304") !=
              std::string::npos &&
              hands.find("\"adjusted_payout\":[") != std::string::npos,
          "adjusted all-in details were not logged");
  require(felt::replay_match_log(directory).hands_verified == 2,
          "adjusted all-in log did not replay");
}

void test_log_replay_and_rerun(const std::string& directory,
                               const std::string& artifact_path) {
  create_log(directory, artifact_path);
  const std::string summary =
      read_file(std::filesystem::path(directory) / "summary.json");
  const std::string hands =
      read_file(std::filesystem::path(directory) / "hands.jsonl");
  require(summary.find("\"schema_version\": 2") != std::string::npos &&
              summary.find("\"status\": \"complete\"") !=
                  std::string::npos &&
              summary.find("\"sha256\":") != std::string::npos,
          "summary omitted required metadata");
  require(hands.find("\"cpu_time_ns\":") != std::string::npos &&
              hands.find("\"wall_time_ns\":") != std::string::npos &&
              hands.find("\"adjusted_net\":[") != std::string::npos &&
              hands.find("\"equity\":null") != std::string::npos &&
              hands.find("\"requested\":") != std::string::npos &&
              hands.find("\"applied\":") != std::string::npos,
          "hand log omitted required decision fields");

  const felt::ReplayReport replay = felt::replay_match_log(directory);
  require(replay.hands_verified == 130,
          "replay verified the wrong number of hands");

  AlwaysAllInBot bot_a;
  CheckFoldBot bot_b;
  const felt::RerunReport identical =
      felt::rerun_match_log(directory, bot_a, bot_b);
  require(identical.hands_compared == 130 &&
              identical.hands_different == 0 &&
              identical.decisions_different == 0,
          "identical seed-and-bot rerun differed");

  AlwaysAllInBot changed_b;
  const felt::RerunReport changed =
      felt::rerun_match_log(directory, bot_a, changed_b);
  require(changed.hands_different != 0 && changed.decisions_different != 0,
          "changed bot was not detected by rerun diagnostic");
}

void test_tamper_detection(const std::string& source,
                           const std::string& tampered) {
  std::filesystem::create_directories(tampered);
  write_file(std::filesystem::path(tampered) / "summary.json",
             read_file(std::filesystem::path(source) / "summary.json"));
  std::string hands =
      read_file(std::filesystem::path(source) / "hands.jsonl");
  const std::string original = "\"raw_net\":[100,-100]";
  const std::size_t location = hands.find(original);
  require(location != std::string::npos, "could not locate test payout");
  hands.replace(location, original.size(), "\"raw_net\":[101,-100]");
  write_file(std::filesystem::path(tampered) / "hands.jsonl", hands);

  try {
    (void)felt::replay_match_log(tampered);
  } catch (const std::runtime_error&) {
    return;
  }
  throw std::runtime_error("tampered terminal result passed replay");
}

void test_normalized_action_replay(const std::string& directory,
                                   const std::string& artifact_path) {
  IllegalBot bot_a;
  IllegalBot bot_b;
  felt::MatchConfig config;
  config.hand_count = 2;
  config.match_seed = 91;
  std::array<felt::BotArtifact, 2> artifacts{
      felt::inspect_bot_artifact(artifact_path, std::string(bot_a.name())),
      felt::inspect_bot_artifact(artifact_path, std::string(bot_b.name()))};
  felt::MatchLogWriter writer(directory, config, std::move(artifacts));
  const felt::MatchResult result =
      felt::play_match(config, bot_a, bot_b, &writer);
  writer.finish(result);

  const std::string hands =
      read_file(std::filesystem::path(directory) / "hands.jsonl");
  require(hands.find("\"violation\":2") != std::string::npos,
          "illegal action violation was not logged");
  require(felt::replay_match_log(directory).hands_verified == 2,
          "normalized illegal actions did not replay");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "match_log_test requires output directory and artifact path\n";
    return 2;
  }
  try {
    const std::filesystem::path base(argv[1]);
    std::filesystem::remove_all(base);
    const std::string valid = (base / "valid").string();
    const std::string tampered = (base / "tampered").string();
    const std::string normalized = (base / "normalized").string();
    const std::string adjusted = (base / "adjusted").string();
    test_log_replay_and_rerun(valid, argv[2]);
    test_tamper_detection(valid, tampered);
    test_normalized_action_replay(normalized, argv[2]);
    test_adjusted_log_replay(adjusted, argv[2]);
  } catch (const std::exception& error) {
    std::cerr << "match_log_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

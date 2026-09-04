#ifndef FELT_MATCH_HPP
#define FELT_MATCH_HPP

#include "felt/hand_engine.hpp"

#include <array>
#include <cstdint>

namespace felt {

struct MatchConfig {
  std::uint64_t hand_count{40'000};
  std::uint64_t match_seed{0};
  FeltChips starting_stack{20'000};
  FeltChips small_blind{50};
  FeltChips big_blind{100};
  std::uint64_t decision_cap_us{2'000};
  bool duplicate{true};
};

struct MatchHand {
  std::uint64_t hand_index{};
  std::uint64_t deal_index{};
  std::array<std::uint32_t, 2> bot_index_by_position{};
  HandCards cards;
  HandResult result;
};

class MatchObserver {
 public:
  virtual ~MatchObserver() = default;
  virtual void on_hand(const MatchHand& hand) = 0;
};

struct MatchResult {
  std::uint64_t hand_count{};
  std::array<FeltChips, 2> net_by_bot{};
  std::array<std::array<FeltChips, 2>, 2> net_by_bot_and_position{};
};

void validate_match_config(const MatchConfig& config);

[[nodiscard]] HandCards deal_hand(std::uint64_t match_seed,
                                  std::uint64_t deal_index);

[[nodiscard]] MatchResult play_match(const MatchConfig& config,
                                     BotRunner& bot_a,
                                     BotRunner& bot_b,
                                     MatchObserver* observer = nullptr);

}  // namespace felt

#endif

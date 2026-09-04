#ifndef FELT_HAND_ENGINE_HPP
#define FELT_HAND_ENGINE_HPP

#include "felt/bot_runner.hpp"
#include "felt/card.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace felt {

struct HandConfig {
  FeltChips starting_stack{20'000};
  FeltChips small_blind{50};
  FeltChips big_blind{100};
  std::uint64_t decision_cap_us{2'000};
  std::uint64_t match_seed{0};
  std::uint64_t randomness_index{0};
};

struct HandCards {
  std::array<std::array<Card, 2>, 2> hole{};
  std::array<Card, 5> board{};
};

enum class ActionViolation : std::uint32_t {
  none = 0,
  nonzero_reserved = 1,
  unknown_action = 2,
  illegal_action = 3,
  invalid_raise_amount = 4,
  decision_cap_exceeded = 5,
};

struct DecisionRecord {
  std::uint32_t position{};
  std::uint32_t street{};
  std::uint32_t legal_actions{};
  FeltChips pot{};
  FeltChips my_stack{};
  FeltChips opp_stack{};
  FeltChips my_street_contribution{};
  FeltChips opp_street_contribution{};
  FeltChips to_call{};
  FeltChips min_raise_to{};
  FeltChips max_raise_to{};
  std::uint64_t decision_random{};
  FeltAction requested{};
  FeltAction applied{};
  ActionViolation violation{ActionViolation::none};
  std::uint64_t cpu_time_ns{};
  std::uint64_t wall_time_ns{};
};

enum class HandEndReason : std::uint32_t {
  fold = 1,
  showdown = 2,
};

struct HandResult {
  HandEndReason reason{HandEndReason::showdown};
  std::uint32_t ending_street{FELT_STREET_PREFLOP};
  std::uint32_t folded_position{UINT32_MAX};
  std::array<FeltChips, 2> committed{};
  std::array<FeltChips, 2> raw_payout{};
  std::array<FeltChips, 2> raw_net{};
  bool equity_adjusted{};
  std::uint64_t equity_boards{};
  std::array<std::uint64_t, 2> equity_wins{};
  std::uint64_t equity_ties{};
  std::array<FeltChips, 2> adjusted_payout{};
  std::array<FeltChips, 2> adjusted_net{};
  std::array<std::uint16_t, 2> showdown_rank{};
  std::vector<FeltActionEvent> events;
  std::vector<DecisionRecord> decisions;
};

[[nodiscard]] HandResult play_hand(
    const HandConfig& config,
    const HandCards& cards,
    const std::array<BotRunner*, 2>& positional_bots);

}  // namespace felt

#endif

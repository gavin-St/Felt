#include "felt/card.hpp"
#include "felt/equity.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

felt::HandCards make_cards(const std::array<const char*, 2>& button,
                           const std::array<const char*, 2>& big_blind,
                           const std::array<const char*, 5>& board) {
  felt::HandCards cards;
  for (std::size_t index = 0; index < 2; ++index) {
    cards.hole[0][index] = felt::card_from_string(button[index]);
    cards.hole[1][index] = felt::card_from_string(big_blind[index]);
  }
  for (std::size_t index = 0; index < 5; ++index) {
    cards.board[index] = felt::card_from_string(board[index]);
  }
  return cards;
}

void test_turn_oracle() {
  const felt::HandCards cards =
      make_cards({"Ac", "Ad"}, {"Kc", "Kd"},
                 {"2s", "3d", "7h", "8s", "4c"});
  felt::ExactEquityCalculator calculator;
  const felt::EquityCounts equity =
      calculator.calculate(cards, FELT_STREET_TURN);
  require(equity.boards == 44 && equity.wins[0] == 42 &&
              equity.wins[1] == 2 && equity.ties == 0,
          "turn equity did not match the hand-count oracle");
}

void test_flop_board_count() {
  const felt::HandCards cards =
      make_cards({"Ah", "Kh"}, {"Qc", "Qd"},
                 {"Jh", "Th", "2c", "3d", "4s"});
  felt::ExactEquityCalculator calculator;
  const felt::EquityCounts equity =
      calculator.calculate(cards, FELT_STREET_FLOP);
  require(equity.boards == 990 &&
              equity.wins[0] + equity.wins[1] + equity.ties == equity.boards,
          "flop equity enumeration count was wrong");
}

void test_preflop_cache_and_swap() {
  const felt::HandCards cards =
      make_cards({"Ac", "Ad"}, {"Kc", "Kd"},
                 {"2s", "3d", "7h", "8s", "4c"});
  felt::ExactEquityCalculator calculator;
  const felt::EquityCounts first =
      calculator.calculate(cards, FELT_STREET_PREFLOP);
  require(first.boards == 1'712'304 &&
              first.wins[0] + first.wins[1] + first.ties == first.boards &&
              first.wins[0] * 100U > first.boards * 80U &&
              first.wins[0] * 100U < first.boards * 84U &&
              calculator.preflop_cache_misses() == 1 &&
              calculator.preflop_cache_hits() == 0,
          "preflop equity or initial cache accounting was wrong");

  felt::HandCards swapped = cards;
  std::swap(swapped.hole[0], swapped.hole[1]);
  const felt::EquityCounts second =
      calculator.calculate(swapped, FELT_STREET_PREFLOP);
  require(second.boards == first.boards && second.wins[0] == first.wins[1] &&
              second.wins[1] == first.wins[0] &&
              second.ties == first.ties &&
              calculator.preflop_cache_size() == 1 &&
              calculator.preflop_cache_hits() == 1 &&
              calculator.preflop_cache_misses() == 1,
          "swapped preflop matchup did not use the canonical cache entry");
}

void test_integer_payout() {
  const felt::EquityCounts half{2, {1, 1}, 0};
  require(felt::equity_payout(5, half) ==
              std::array<FeltChips, 2>{2, 3},
          "odd equity chip was not assigned to the big blind");

  const FeltChips maximum = std::numeric_limits<FeltChips>::max();
  const felt::EquityCounts all_button{1, {1, 0}, 0};
  require(felt::equity_payout(maximum, all_button) ==
              std::array<FeltChips, 2>{maximum, 0},
          "wide payout multiplication overflowed");
}

void test_invalid_cards() {
  felt::HandCards cards =
      make_cards({"Ac", "Ad"}, {"Kc", "Kd"},
                 {"2s", "3d", "7h", "8s", "4c"});
  cards.hole[1][0] = cards.hole[0][0];
  felt::ExactEquityCalculator calculator;
  try {
    (void)calculator.calculate(cards, FELT_STREET_PREFLOP);
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("duplicate preflop hole cards were accepted");
}

}  // namespace

int main() {
  try {
    test_turn_oracle();
    test_flop_board_count();
    test_preflop_cache_and_swap();
    test_integer_payout();
    test_invalid_cards();
  } catch (const std::exception& error) {
    std::cerr << "equity_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

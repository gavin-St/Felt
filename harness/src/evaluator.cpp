#include "felt/evaluator.hpp"

#include "omp/HandEvaluator.h"

#include <array>
#include <stdexcept>

namespace felt {
namespace {

HandRank evaluate_unchecked(const std::array<Card, 7>& cards) {
  omp::Hand hand = omp::Hand::empty();
  for (const Card card : cards) {
    hand += omp::Hand(card);
  }

  static const omp::HandEvaluator evaluator;
  return evaluator.evaluate(hand);
}

}  // namespace

HandRank evaluate7(const std::array<Card, 7>& cards) {
  std::array<bool, kCardCount> seen{};
  for (const Card card : cards) {
    if (!is_valid_card(card) || seen[card]) {
      throw std::invalid_argument("evaluate7 requires seven distinct cards");
    }
    seen[card] = true;
  }
  return evaluate_unchecked(cards);
}

HandRank evaluate7_unchecked(const std::array<Card, 7>& cards) {
  return evaluate_unchecked(cards);
}

}  // namespace felt

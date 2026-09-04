#include "felt/evaluator.hpp"
#include "felt/random.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <vector>

namespace {

std::array<felt::Card, 7> random_hand(
    felt::Xoshiro256PlusPlus& random) {
  felt::Deck deck{};
  for (std::uint8_t card = 0; card < felt::kCardCount; ++card) {
    deck[card] = card;
  }

  std::array<felt::Card, 7> hand{};
  for (std::size_t index = 0; index < hand.size(); ++index) {
    const std::size_t other =
        index + static_cast<std::size_t>(
                    random.bounded(deck.size() - index));
    std::swap(deck[index], deck[other]);
    hand[index] = deck[index];
  }
  return hand;
}

}  // namespace

int main() {
  try {
    constexpr std::size_t hand_count = 1'000'000;
    constexpr std::size_t repeat_count = 100;
    felt::Xoshiro256PlusPlus random(
        felt::sha256("felt/evaluator-benchmark/v1"));
    std::vector<std::array<felt::Card, 7>> hands;
    hands.reserve(hand_count);
    for (std::size_t index = 0; index < hand_count; ++index) {
      hands.push_back(random_hand(random));
    }

    (void)felt::evaluate7_unchecked(hands.front());
    std::uint64_t checksum = 0;
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t repeat = 0; repeat < repeat_count; ++repeat) {
      for (const auto& hand : hands) {
        checksum += felt::evaluate7_unchecked(hand);
      }
    }
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();

    const std::size_t evaluation_count = hand_count * repeat_count;
    std::cout << evaluation_count / seconds << " evaluations/s"
              << " (" << seconds << " s, checksum " << checksum << ")\n";
  } catch (const std::exception& error) {
    std::cerr << "evaluator_benchmark: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

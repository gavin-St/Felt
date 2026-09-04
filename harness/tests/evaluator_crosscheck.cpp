#include "reference_evaluator.hpp"

#include "felt/evaluator.hpp"
#include "felt/random.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

std::uint64_t parse_count(int argc, char** argv) {
  if (argc == 1) {
    return 100'000U;
  }
  if (argc != 2) {
    throw std::invalid_argument("usage: felt_evaluator_crosscheck [HANDS]");
  }

  std::size_t consumed = 0;
  const std::string text(argv[1]);
  const std::uint64_t count = std::stoull(text, &consumed);
  if (consumed != text.size() || count == 0U) {
    throw std::invalid_argument("HANDS must be a positive integer");
  }
  return count;
}

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

int main(int argc, char** argv) {
  try {
    const std::uint64_t count = parse_count(argc, argv);
    felt::Xoshiro256PlusPlus random(
        felt::sha256("felt/evaluator-crosscheck/v1"));
    std::map<felt::tests::ReferenceRank, felt::HandRank> reference_to_omp;
    std::map<felt::HandRank, felt::tests::ReferenceRank> omp_to_reference;

    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < count; ++index) {
      const std::array<felt::Card, 7> cards = random_hand(random);
      const felt::HandRank omp_rank = felt::evaluate7_unchecked(cards);
      const felt::tests::ReferenceRank reference_rank =
          felt::tests::reference_evaluate7(cards);

      if (felt::hand_category(omp_rank) !=
          felt::tests::reference_category(reference_rank) + 1U) {
        throw std::runtime_error("evaluator category mismatch at sample " +
                                 std::to_string(index));
      }

      const auto [reference_it, reference_inserted] =
          reference_to_omp.emplace(reference_rank, omp_rank);
      if (!reference_inserted && reference_it->second != omp_rank) {
        throw std::runtime_error(
            "one reference rank mapped to multiple OMPEval ranks");
      }

      const auto [omp_it, omp_inserted] =
          omp_to_reference.emplace(omp_rank, reference_rank);
      if (!omp_inserted && omp_it->second != reference_rank) {
        throw std::runtime_error(
            "one OMPEval rank mapped to multiple reference ranks");
      }
    }

    felt::HandRank previous_omp = 0;
    bool first = true;
    for (const auto& [reference_rank, omp_rank] : reference_to_omp) {
      (void)reference_rank;
      if (!first && omp_rank <= previous_omp) {
        throw std::runtime_error("evaluator rank ordering mismatch");
      }
      first = false;
      previous_omp = omp_rank;
    }

    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started);
    std::cout << "cross-checked " << count << " hands, observed "
              << reference_to_omp.size() << " distinct ranks in "
              << elapsed.count() << " s\n";
  } catch (const std::exception& error) {
    std::cerr << "evaluator_crosscheck: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

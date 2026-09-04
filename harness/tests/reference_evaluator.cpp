#include "reference_evaluator.hpp"

#include "felt/card.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace felt::tests {
namespace {

ReferenceRank encode(std::uint8_t category,
                     const std::array<std::uint8_t, 5>& ranks) {
  ReferenceRank value = static_cast<ReferenceRank>(category) << 24U;
  for (std::size_t index = 0; index < ranks.size(); ++index) {
    value |= static_cast<ReferenceRank>(ranks[index] + 1U)
             << (20U - static_cast<unsigned>(index) * 4U);
  }
  return value;
}

int straight_high(std::uint16_t rank_mask) {
  for (int high = 12; high >= 4; --high) {
    const std::uint16_t pattern =
        static_cast<std::uint16_t>(UINT16_C(0x1f) << (high - 4));
    if ((rank_mask & pattern) == pattern) {
      return high;
    }
  }

  constexpr std::uint16_t wheel =
      (UINT16_C(1) << 12U) | UINT16_C(0x0f);
  return (rank_mask & wheel) == wheel ? 3 : -1;
}

std::array<std::uint8_t, 5> descending_ranks(
    const std::array<std::uint8_t, 13>& counts,
    std::uint8_t required_count) {
  std::array<std::uint8_t, 5> result{};
  std::size_t output = 0;
  for (int rank = 12; rank >= 0 && output < result.size(); --rank) {
    if (counts[static_cast<std::size_t>(rank)] == required_count) {
      result[output++] = static_cast<std::uint8_t>(rank);
    }
  }
  return result;
}

ReferenceRank evaluate5(const std::array<Card, 5>& cards) {
  std::array<std::uint8_t, 13> rank_counts{};
  std::array<std::uint8_t, 4> suit_counts{};
  std::uint16_t rank_mask = 0;

  for (const Card card : cards) {
    const std::uint8_t rank = card_rank(card);
    ++rank_counts[rank];
    ++suit_counts[card_suit(card)];
    rank_mask |= static_cast<std::uint16_t>(UINT16_C(1) << rank);
  }

  const bool flush =
      std::find(suit_counts.begin(), suit_counts.end(), 5U) != suit_counts.end();
  const int straight = straight_high(rank_mask);
  if (flush && straight >= 0) {
    return encode(8, {static_cast<std::uint8_t>(straight), 0, 0, 0, 0});
  }

  const auto quads = descending_ranks(rank_counts, 4);
  if (std::find(rank_counts.begin(), rank_counts.end(), 4U) !=
      rank_counts.end()) {
    const auto singles = descending_ranks(rank_counts, 1);
    return encode(7, {quads[0], singles[0], 0, 0, 0});
  }

  const auto trips = descending_ranks(rank_counts, 3);
  const auto pairs = descending_ranks(rank_counts, 2);
  const bool has_trips =
      std::find(rank_counts.begin(), rank_counts.end(), 3U) !=
      rank_counts.end();
  const std::size_t pair_count =
      static_cast<std::size_t>(std::count(rank_counts.begin(),
                                         rank_counts.end(), 2U));
  if (has_trips && pair_count >= 1U) {
    return encode(6, {trips[0], pairs[0], 0, 0, 0});
  }

  const auto singles = descending_ranks(rank_counts, 1);
  if (flush) {
    return encode(5, singles);
  }
  if (straight >= 0) {
    return encode(4, {static_cast<std::uint8_t>(straight), 0, 0, 0, 0});
  }
  if (has_trips) {
    return encode(3, {trips[0], singles[0], singles[1], 0, 0});
  }
  if (pair_count >= 2U) {
    return encode(2, {pairs[0], pairs[1], singles[0], 0, 0});
  }
  if (pair_count == 1U) {
    return encode(1, {pairs[0], singles[0], singles[1], singles[2], 0});
  }
  return encode(0, singles);
}

}  // namespace

ReferenceRank reference_evaluate7(const std::array<Card, 7>& cards) {
  ReferenceRank best = 0;
  for (std::size_t a = 0; a < 3U; ++a) {
    for (std::size_t b = a + 1U; b < 4U; ++b) {
      for (std::size_t c = b + 1U; c < 5U; ++c) {
        for (std::size_t d = c + 1U; d < 6U; ++d) {
          for (std::size_t e = d + 1U; e < 7U; ++e) {
            best = std::max(
                best, evaluate5({cards[a], cards[b], cards[c], cards[d],
                                 cards[e]}));
          }
        }
      }
    }
  }
  return best;
}

}  // namespace felt::tests

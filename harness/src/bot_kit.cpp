#include "felt/bot_kit.h"

#include "omp/Hand.h"
#include "omp/HandEvaluator.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace {

constexpr std::uint8_t kCardCount = 52;
constexpr std::uint8_t kRankCount = 13;
constexpr std::uint8_t kSuitCount = 4;
constexpr std::uint8_t kBroadwayStart = 8;  // Ten.

std::uint8_t rank_of(FeltCard card) {
  return static_cast<std::uint8_t>(card >> 2U);
}

std::uint8_t suit_of(FeltCard card) {
  return static_cast<std::uint8_t>(card & 3U);
}

bool add_card(FeltCard card,
              std::array<bool, kCardCount>& seen,
              omp::Hand& hand) {
  if (card >= kCardCount || seen[card]) {
    return false;
  }
  seen[card] = true;
  hand += omp::Hand(card);
  return true;
}

bool valid_board(const FeltCard* board,
                 std::uint8_t board_count,
                 std::array<bool, kCardCount>& seen,
                 omp::Hand& hand) {
  if (board == nullptr || board_count < 3U || board_count > 5U) {
    return false;
  }
  for (std::uint8_t index = 0; index < board_count; ++index) {
    if (!add_card(board[index], seen, hand)) {
      return false;
    }
  }
  return true;
}

const omp::HandEvaluator& evaluator() {
  static const omp::HandEvaluator instance;
  return instance;
}

FeltMadeCategory made_category(std::uint16_t rank) {
  const std::uint16_t category = static_cast<std::uint16_t>(rank >> 12U);
  return static_cast<FeltMadeCategory>(category - 1U);
}

std::uint8_t max_rank(const std::array<std::uint8_t, kRankCount>& counts) {
  for (std::uint8_t rank = kRankCount; rank > 0U; --rank) {
    if (counts[rank - 1U] != 0U) {
      return static_cast<std::uint8_t>(rank - 1U);
    }
  }
  return FELT_NO_RANK;
}

std::uint8_t min_rank(const std::array<std::uint8_t, kRankCount>& counts) {
  for (std::uint8_t rank = 0; rank < kRankCount; ++rank) {
    if (counts[rank] != 0U) {
      return rank;
    }
  }
  return FELT_NO_RANK;
}

std::uint8_t window_count(
    const std::array<std::uint8_t, kRankCount>& rank_counts,
    std::uint8_t start) {
  std::uint8_t count = 0;
  for (std::uint8_t rank = start; rank < start + 5U; ++rank) {
    count = static_cast<std::uint8_t>(count + (rank_counts[rank] != 0U));
  }
  return count;
}

std::uint8_t max_straight_window(
    const std::array<std::uint8_t, kRankCount>& rank_counts) {
  std::uint8_t best = 0;
  for (std::uint8_t start = 0; start <= 8U; ++start) {
    best = std::max(best, window_count(rank_counts, start));
  }

  // Wheel: A, 2, 3, 4, 5.
  std::uint8_t wheel = static_cast<std::uint8_t>(rank_counts[12] != 0U);
  for (std::uint8_t rank = 0; rank <= 3U; ++rank) {
    wheel = static_cast<std::uint8_t>(wheel + (rank_counts[rank] != 0U));
  }
  return std::max(best, wheel);
}

}  // namespace

extern "C" void felt_bot_kit_warmup(void) {
  (void)evaluator();
}

extern "C" FeltMadeHand felt_made_hand(const FeltCard hole[2],
                                        const FeltCard* board,
                                        std::uint8_t board_count) {
  FeltMadeHand result{};
  result.hole_kicker_rank = FELT_NO_RANK;

  if (hole == nullptr) {
    return result;
  }

  std::array<bool, kCardCount> seen{};
  omp::Hand hand = omp::Hand::empty();
  if (!valid_board(board, board_count, seen, hand) ||
      !add_card(hole[0], seen, hand) || !add_card(hole[1], seen, hand)) {
    return result;
  }

  result.rank = evaluator().evaluate(hand);
  result.category = made_category(result.rank);
  result.valid = true;

  std::array<std::uint8_t, kRankCount> board_ranks{};
  for (std::uint8_t index = 0; index < board_count; ++index) {
    ++board_ranks[rank_of(board[index])];
  }

  const std::uint8_t first_rank = rank_of(hole[0]);
  const std::uint8_t second_rank = rank_of(hole[1]);
  const bool pocket_pair = first_rank == second_rank;

  if (result.category == FELT_MADE_ONE_PAIR) {
    if (pocket_pair) {
      result.pair_relation =
          first_rank > max_rank(board_ranks) ? FELT_PAIR_OVERPAIR
                                             : FELT_PAIR_UNDERPAIR;
    } else {
      const bool first_pairs = board_ranks[first_rank] != 0U;
      const bool second_pairs = board_ranks[second_rank] != 0U;
      if (first_pairs != second_pairs) {
        const std::uint8_t paired_rank = first_pairs ? first_rank : second_rank;
        result.hole_kicker_rank = first_pairs ? second_rank : first_rank;
        if (paired_rank == max_rank(board_ranks)) {
          result.pair_relation = FELT_PAIR_TOP;
        } else if (paired_rank == min_rank(board_ranks)) {
          result.pair_relation = FELT_PAIR_BOTTOM;
        } else {
          result.pair_relation = FELT_PAIR_MIDDLE;
        }
      } else {
        // The pair is entirely on the board; the best hole rank is the useful
        // kicker descriptor, but this is not top/middle/bottom pair.
        result.hole_kicker_rank = std::max(first_rank, second_rank);
      }
    }
  }

  if (result.category == FELT_MADE_TRIPS) {
    result.is_set = pocket_pair && board_ranks[first_rank] == 1U;
    result.is_trips =
        !pocket_pair &&
        (board_ranks[first_rank] >= 2U || board_ranks[second_rank] >= 2U);
  }

  if (board_count == 5U) {
    omp::Hand board_hand = omp::Hand::empty();
    for (std::uint8_t index = 0; index < board_count; ++index) {
      board_hand += omp::Hand(board[index]);
    }
    const std::uint16_t board_rank = evaluator().evaluate(board_hand);
    result.plays_board = result.rank == board_rank;
    result.improves_board = result.rank > board_rank;
  }

  return result;
}

extern "C" FeltBoardTexture felt_board_texture(const FeltCard* board,
                                                std::uint8_t board_count) {
  FeltBoardTexture result{};
  result.high_rank = FELT_NO_RANK;

  std::array<bool, kCardCount> seen{};
  omp::Hand ignored = omp::Hand::empty();
  if (!valid_board(board, board_count, seen, ignored)) {
    return result;
  }

  result.high_rank = 0U;
  std::array<std::uint8_t, kRankCount> rank_counts{};
  std::array<std::uint8_t, kSuitCount> suit_counts{};
  for (std::uint8_t index = 0; index < board_count; ++index) {
    const std::uint8_t rank = rank_of(board[index]);
    ++rank_counts[rank];
    ++suit_counts[suit_of(board[index])];
    result.high_rank = std::max(result.high_rank, rank);
    if (rank >= kBroadwayStart) {
      ++result.broadway_count;
    }
  }

  for (const std::uint8_t count : rank_counts) {
    result.distinct_rank_count =
        static_cast<std::uint8_t>(result.distinct_rank_count + (count != 0U));
    result.pair_count =
        static_cast<std::uint8_t>(result.pair_count + (count == 2U));
    result.trips_on_board = result.trips_on_board || count == 3U;
    result.quads_on_board = result.quads_on_board || count == 4U;
  }
  for (const std::uint8_t count : suit_counts) {
    result.max_suit_count = std::max(result.max_suit_count, count);
  }

  result.max_cards_in_five_rank_window = max_straight_window(rank_counts);
  result.straight_on_board = result.max_cards_in_five_rank_window == 5U;
  result.flush_on_board = result.max_suit_count == 5U;
  result.valid = true;
  return result;
}

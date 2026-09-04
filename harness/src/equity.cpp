#include "felt/equity.hpp"

#include "felt/evaluator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace felt {
namespace {

constexpr std::uint32_t kComboKeyBits = 12;

std::uint32_t combo_key(std::array<Card, 2> cards) {
  if (cards[1] < cards[0]) {
    std::swap(cards[0], cards[1]);
  }
  return static_cast<std::uint32_t>(cards[0]) * kCardCount + cards[1];
}

std::uint32_t matchup_key(std::uint32_t first, std::uint32_t second) {
  return (first << kComboKeyBits) | second;
}

EquityCounts reversed(EquityCounts counts) {
  std::swap(counts.wins[0], counts.wins[1]);
  return counts;
}

std::size_t visible_board_count(std::uint32_t street) {
  switch (street) {
    case FELT_STREET_PREFLOP:
      return 0;
    case FELT_STREET_FLOP:
      return 3;
    case FELT_STREET_TURN:
      return 4;
    default:
      throw std::invalid_argument(
          "exact equity requires a preflop, flop, or turn all-in");
  }
}

void validate_known_cards(const HandCards& cards, std::size_t board_count) {
  std::array<bool, kCardCount> seen{};
  for (const auto& hole : cards.hole) {
    for (const Card card : hole) {
      if (!is_valid_card(card) || seen[card]) {
        throw std::invalid_argument(
            "equity requires distinct valid known cards");
      }
      seen[card] = true;
    }
  }
  for (std::size_t index = 0; index < board_count; ++index) {
    const Card card = cards.board[index];
    if (!is_valid_card(card) || seen[card]) {
      throw std::invalid_argument("equity requires a distinct visible board");
    }
    seen[card] = true;
  }
}

std::array<Card, 52> remaining_cards(const HandCards& cards,
                                     std::size_t board_count,
                                     std::size_t& remaining_count) {
  std::array<bool, kCardCount> known{};
  for (const auto& hole : cards.hole) {
    known[hole[0]] = true;
    known[hole[1]] = true;
  }
  for (std::size_t index = 0; index < board_count; ++index) {
    known[cards.board[index]] = true;
  }

  std::array<Card, 52> remaining{};
  remaining_count = 0;
  for (Card card = 0; card < kCardCount; ++card) {
    if (!known[card]) {
      remaining[remaining_count++] = card;
    }
  }
  return remaining;
}

void score_board(const std::array<Card, 5>& board,
                 const HandCards& cards,
                 EquityCounts& counts) {
  std::array<Card, 7> button{cards.hole[0][0], cards.hole[0][1],
                             board[0],          board[1],
                             board[2],          board[3],
                             board[4]};
  std::array<Card, 7> big_blind{cards.hole[1][0], cards.hole[1][1],
                                board[0],          board[1],
                                board[2],          board[3],
                                board[4]};
  const HandRank button_rank = evaluate7_unchecked(button);
  const HandRank big_blind_rank = evaluate7_unchecked(big_blind);
  ++counts.boards;
  if (button_rank > big_blind_rank) {
    ++counts.wins[0];
  } else if (big_blind_rank > button_rank) {
    ++counts.wins[1];
  } else {
    ++counts.ties;
  }
}

EquityCounts enumerate_equity(const HandCards& cards, std::uint32_t street) {
  const std::size_t board_count = visible_board_count(street);
  validate_known_cards(cards, board_count);
  std::size_t remaining_count = 0;
  const std::array<Card, 52> remaining =
      remaining_cards(cards, board_count, remaining_count);
  std::array<Card, 5> board = cards.board;
  EquityCounts counts;

  if (street == FELT_STREET_TURN) {
    for (std::size_t river = 0; river < remaining_count; ++river) {
      board[4] = remaining[river];
      score_board(board, cards, counts);
    }
  } else if (street == FELT_STREET_FLOP) {
    for (std::size_t turn = 0; turn + 1U < remaining_count; ++turn) {
      board[3] = remaining[turn];
      for (std::size_t river = turn + 1U; river < remaining_count; ++river) {
        board[4] = remaining[river];
        score_board(board, cards, counts);
      }
    }
  } else {
    for (std::size_t a = 0; a + 4U < remaining_count; ++a) {
      board[0] = remaining[a];
      for (std::size_t b = a + 1U; b + 3U < remaining_count; ++b) {
        board[1] = remaining[b];
        for (std::size_t c = b + 1U; c + 2U < remaining_count; ++c) {
          board[2] = remaining[c];
          for (std::size_t d = c + 1U; d + 1U < remaining_count; ++d) {
            board[3] = remaining[d];
            for (std::size_t e = d + 1U; e < remaining_count; ++e) {
              board[4] = remaining[e];
              score_board(board, cards, counts);
            }
          }
        }
      }
    }
  }
  return counts;
}

}  // namespace

EquityCounts ExactEquityCalculator::calculate(const HandCards& cards,
                                               std::uint32_t all_in_street) {
  if (all_in_street != FELT_STREET_PREFLOP) {
    return enumerate_equity(cards, all_in_street);
  }

  validate_known_cards(cards, 0);

  const std::uint32_t button_combo = combo_key(cards.hole[0]);
  const std::uint32_t big_blind_combo = combo_key(cards.hole[1]);
  const bool swapped = big_blind_combo < button_combo;
  const std::uint32_t first = swapped ? big_blind_combo : button_combo;
  const std::uint32_t second = swapped ? button_combo : big_blind_combo;
  const std::uint32_t key = matchup_key(first, second);
  const auto found = preflop_cache_.find(key);
  if (found != preflop_cache_.end()) {
    ++preflop_cache_hits_;
    return swapped ? reversed(found->second) : found->second;
  }

  HandCards canonical = cards;
  if (swapped) {
    std::swap(canonical.hole[0], canonical.hole[1]);
  }
  EquityCounts counts = enumerate_equity(canonical, FELT_STREET_PREFLOP);
  preflop_cache_.emplace(key, counts);
  ++preflop_cache_misses_;
  return swapped ? reversed(counts) : counts;
}

std::size_t ExactEquityCalculator::preflop_cache_size() const noexcept {
  return preflop_cache_.size();
}

std::uint64_t ExactEquityCalculator::preflop_cache_hits() const noexcept {
  return preflop_cache_hits_;
}

std::uint64_t ExactEquityCalculator::preflop_cache_misses() const noexcept {
  return preflop_cache_misses_;
}

std::array<FeltChips, 2> equity_payout(FeltChips pot,
                                      const EquityCounts& equity) {
  using Wide = unsigned __int128;
  if (pot < 0 || equity.boards == 0 ||
      static_cast<Wide>(equity.wins[0]) + equity.wins[1] + equity.ties !=
          equity.boards) {
    throw std::invalid_argument("invalid equity payout inputs");
  }
  const Wide numerator =
      static_cast<Wide>(2U) * equity.wins[0] + equity.ties;
  const Wide denominator = static_cast<Wide>(2U) * equity.boards;
  const Wide button_wide = static_cast<Wide>(pot) * numerator / denominator;
  if (button_wide >
      static_cast<Wide>(std::numeric_limits<FeltChips>::max())) {
    throw std::overflow_error("equity payout overflowed FeltChips");
  }
  const FeltChips button = static_cast<FeltChips>(button_wide);
  return {button, pot - button};
}

void apply_equity_adjustment(HandResult& result,
                             const HandCards& cards,
                             FeltChips starting_stack,
                             ExactEquityCalculator& calculator) {
  result.adjusted_payout = result.raw_payout;
  result.adjusted_net = result.raw_net;
  if (result.reason != HandEndReason::showdown ||
      result.ending_street >= FELT_STREET_RIVER ||
      result.committed[0] != starting_stack ||
      result.committed[1] != starting_stack) {
    return;
  }

  const EquityCounts equity = calculator.calculate(cards, result.ending_street);
  result.equity_adjusted = true;
  result.equity_boards = equity.boards;
  result.equity_wins = equity.wins;
  result.equity_ties = equity.ties;
  result.adjusted_payout =
      equity_payout(result.committed[0] + result.committed[1], equity);
  for (std::size_t position = 0; position < 2; ++position) {
    result.adjusted_net[position] =
        result.adjusted_payout[position] - result.committed[position];
  }
}

}  // namespace felt

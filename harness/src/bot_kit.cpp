#include "felt/bot_kit.h"

#include "omp/Hand.h"
#include "omp/HandEvaluator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

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

int best_straight_high(const std::array<bool, kRankCount>& ranks) {
  int best = -1;
  for (std::uint8_t start = 0; start <= 8U; ++start) {
    bool complete = true;
    for (std::uint8_t rank = start; rank < start + 5U; ++rank) {
      complete = complete && ranks[rank];
    }
    if (complete) {
      best = static_cast<int>(start + 4U);
    }
  }
  if (ranks[12] && ranks[0] && ranks[1] && ranks[2] && ranks[3]) {
    best = std::max(best, 3);
  }
  return best;
}

bool has_open_ended_core(const std::array<bool, kRankCount>& ranks) {
  for (std::uint8_t start = 0U; start <= 8U; ++start) {
    bool four_in_a_row = true;
    for (std::uint8_t rank = start; rank < start + 4U; ++rank) {
      four_in_a_row = four_in_a_row && ranks[rank];
    }
    if (four_in_a_row) {
      return true;
    }
  }
  return false;
}

FeltAction passive_action(const FeltGameState* state) {
  FeltAction action{};
  if (state != nullptr &&
      (state->legal_actions & FELT_LEGAL_CALL) != 0U) {
    action.type = FELT_ACTION_CALL;
  } else if (state != nullptr &&
             (state->legal_actions & FELT_LEGAL_CHECK) != 0U) {
    action.type = FELT_ACTION_CHECK;
  } else {
    action.type = FELT_ACTION_FOLD;
  }
  return action;
}

FeltAction raise_to_target(const FeltGameState* state, FeltChips target) {
  if (state == nullptr ||
      (state->legal_actions & FELT_LEGAL_RAISE_TO) == 0U) {
    return passive_action(state);
  }

  FeltAction action{};
  action.type = FELT_ACTION_RAISE_TO;
  if (state->max_raise_to <= state->min_raise_to) {
    action.amount_to = state->max_raise_to;
  } else {
    action.amount_to =
        std::clamp(target, state->min_raise_to, state->max_raise_to);
  }
  return action;
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

extern "C" FeltDraws felt_draws(const FeltCard hole[2],
                                  const FeltCard* board,
                                  std::uint8_t board_count) {
  FeltDraws result{};
  if (hole == nullptr) {
    return result;
  }

  std::array<bool, kCardCount> seen{};
  omp::Hand ignored = omp::Hand::empty();
  if (!valid_board(board, board_count, seen, ignored) ||
      !add_card(hole[0], seen, ignored) ||
      !add_card(hole[1], seen, ignored)) {
    return result;
  }
  result.valid = true;
  if (board_count == 5U) {
    return result;
  }

  const FeltMadeHand made = felt_made_hand(hole, board, board_count);
  const std::uint8_t first_rank = rank_of(hole[0]);
  const std::uint8_t second_rank = rank_of(hole[1]);

  std::array<bool, kRankCount> ranks{};
  std::array<bool, kRankCount> board_ranks{};
  std::array<std::uint8_t, kSuitCount> suit_counts{};
  ranks[first_rank] = true;
  ranks[second_rank] = true;
  ++suit_counts[suit_of(hole[0])];
  ++suit_counts[suit_of(hole[1])];
  std::uint8_t board_high = 0U;
  for (std::uint8_t index = 0; index < board_count; ++index) {
    ranks[rank_of(board[index])] = true;
    board_ranks[rank_of(board[index])] = true;
    ++suit_counts[suit_of(board[index])];
    board_high = std::max(board_high, rank_of(board[index]));
  }

  std::array<bool, kCardCount> improving_cards{};
  if (made.category == FELT_MADE_HIGH_CARD &&
      (first_rank > board_high || second_rank > board_high)) {
    result.flags |= FELT_DRAW_OVERCARDS;
    for (FeltCard candidate = 0; candidate < kCardCount; ++candidate) {
      if (!seen[candidate] &&
          (rank_of(candidate) == first_rank ||
           rank_of(candidate) == second_rank)) {
        improving_cards[candidate] = true;
      }
    }
  }

  if (best_straight_high(ranks) < 0) {
    std::array<bool, kRankCount> straight_out_ranks{};
    for (std::uint8_t candidate_rank = 0; candidate_rank < kRankCount;
         ++candidate_rank) {
      if (ranks[candidate_rank]) {
        continue;
      }
      auto with_candidate = ranks;
      auto board_with_candidate = board_ranks;
      with_candidate[candidate_rank] = true;
      board_with_candidate[candidate_rank] = true;
      if (best_straight_high(with_candidate) >
          best_straight_high(board_with_candidate)) {
        straight_out_ranks[candidate_rank] = true;
      }
    }

    std::uint8_t out_rank_count = 0U;
    for (std::uint8_t rank = 0; rank < kRankCount; ++rank) {
      if (!straight_out_ranks[rank]) {
        continue;
      }
      ++out_rank_count;
      for (FeltCard candidate = 0; candidate < kCardCount; ++candidate) {
        if (!seen[candidate] && rank_of(candidate) == rank) {
          improving_cards[candidate] = true;
          ++result.straight_next_cards;
        }
      }
    }
    if (out_rank_count == 1U) {
      result.flags |= FELT_DRAW_GUTSHOT;
    } else if (out_rank_count >= 2U && has_open_ended_core(ranks)) {
      result.flags |= FELT_DRAW_OPEN_ENDED;
    } else if (out_rank_count >= 2U) {
      result.flags |= FELT_DRAW_DOUBLE_GUTSHOT;
    }
  }

  for (std::uint8_t suit = 0; suit < kSuitCount; ++suit) {
    const bool hole_has_suit = suit_of(hole[0]) == suit ||
                               suit_of(hole[1]) == suit;
    if (suit_counts[suit] != 4U || !hole_has_suit) {
      continue;
    }
    result.flags |= FELT_DRAW_FLUSH;
    for (FeltCard candidate = 0; candidate < kCardCount; ++candidate) {
      if (!seen[candidate] && suit_of(candidate) == suit) {
        improving_cards[candidate] = true;
        ++result.flush_next_cards;
      }
    }

    for (std::uint8_t rank = kRankCount; rank > 0U; --rank) {
      const FeltCard suited_card =
          static_cast<FeltCard>((rank - 1U) * kSuitCount + suit);
      if (!seen[suited_card] || suited_card == hole[0] ||
          suited_card == hole[1]) {
        result.nut_flush_draw =
            suited_card == hole[0] || suited_card == hole[1];
        break;
      }
    }
  }

  for (bool improves : improving_cards) {
    result.improving_next_cards = static_cast<std::uint8_t>(
        result.improving_next_cards + (improves ? 1U : 0U));
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

extern "C" FeltAction felt_check_or_fold(const FeltGameState* state) {
  FeltAction action{};
  if (state != nullptr &&
      (state->legal_actions & FELT_LEGAL_CHECK) != 0U) {
    action.type = FELT_ACTION_CHECK;
  } else {
    action.type = FELT_ACTION_FOLD;
  }
  return action;
}

extern "C" FeltAction felt_call_or_check(const FeltGameState* state) {
  return passive_action(state);
}

extern "C" FeltAction felt_raise_to_pot_fraction(
    const FeltGameState* state, double fraction) {
  if (state == nullptr || !std::isfinite(fraction) || fraction <= 0.0 ||
      state->pot < 0 || state->my_street_contribution < 0) {
    return passive_action(state);
  }
  const long double after_call_pot =
      static_cast<long double>(state->pot) + state->to_call;
  const long double addition = after_call_pot * fraction;
  const FeltChips maximum = std::numeric_limits<FeltChips>::max();
  const long double base =
      static_cast<long double>(state->my_street_contribution) + state->to_call;
  const long double room = static_cast<long double>(maximum) - base;
  const FeltChips target = addition >= room
                               ? maximum
                               : static_cast<FeltChips>(base + addition);
  return raise_to_target(state, target);
}

extern "C" FeltAction felt_raise_to_multiple(const FeltGameState* state,
                                               std::uint32_t multiple) {
  if (state == nullptr || multiple == 0U ||
      state->opp_street_contribution <= 0) {
    return passive_action(state);
  }
  const FeltChips maximum = std::numeric_limits<FeltChips>::max();
  const FeltChips target =
      state->opp_street_contribution > maximum / multiple
          ? maximum
          : state->opp_street_contribution * multiple;
  return raise_to_target(state, target);
}

extern "C" FeltAction felt_all_in(const FeltGameState* state) {
  if (state == nullptr ||
      (state->legal_actions & FELT_LEGAL_RAISE_TO) == 0U) {
    return passive_action(state);
  }
  FeltAction action{};
  action.type = FELT_ACTION_RAISE_TO;
  action.amount_to = state->max_raise_to;
  return action;
}

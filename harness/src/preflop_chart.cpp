#include "felt/bot_kit.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace {

constexpr std::uint8_t kRankCount = 13;
constexpr std::uint8_t kAce = 12;
constexpr std::uint8_t kKing = 11;
constexpr std::uint8_t kQueen = 10;
constexpr std::uint8_t kJack = 9;
constexpr std::uint8_t kTen = 8;

using ChartMatrix = std::array<std::string_view, kRankCount>;

// Rows and columns run A..2, matching the user-supplied charts. Above the
// diagonal is suited, below it is offsuit. V=value raise, B=bluff raise,
// C=passive (limp/call/check), F=fold.
constexpr ChartMatrix kSmallBlindFirstIn{
    "CVVVVVVVVVVVV", "CCVVVVCCCCCCC", "VVVVVVCCCCCCC",
    "VVCVVCCCCCBBB", "VVCCVCCCCCBBB", "CCCCCVCCCBBBB",
    "CCCCCCVCCBBFB", "CCCCCCCVCCBFF", "BCCBBBBCCCCBF",
    "CCBFFFFFCCCBF", "CCBFFFFFFFCBF", "CBBFFFFFFFFCC",
    "CBBFFFFFFFFFC"};

constexpr ChartMatrix kBigBlindVsOpen{
    "VVVVVCCCCCCCC", "VVVVVCCCCCCCC", "VVVVVCCCCCCCC",
    "VCCVVBCCCCCCC", "CCCCVBBCCCCCC", "CCCCCVBCCCCCC",
    "CCCCCCCBCCCCC", "CCCCCCCCBCCCC", "CCCCCCCBCBCCC",
    "CCCBBFFBBCBCC", "CCBFFFFFBBCCC", "CBBFFFFFFFFCC",
    "BBBFFFFFFFFFC"};

struct Pattern {
  std::uint8_t high;
  std::uint8_t low;
  bool suited;
};

constexpr Pattern pair(std::uint8_t rank) {
  return Pattern{rank, rank, false};
}

constexpr Pattern suited(std::uint8_t high, std::uint8_t low) {
  return Pattern{high, low, true};
}

constexpr Pattern offsuit(std::uint8_t high, std::uint8_t low) {
  return Pattern{high, low, false};
}

constexpr std::array<Pattern, 8> kBbVsLimpValuePairs{
    pair(kAce), pair(kKing), pair(kQueen), pair(kJack),
    pair(kTen), pair(7), pair(6), pair(5)};

constexpr std::array<Pattern, 7> kBbVsLimpAddedBluff{
    suited(kAce, 3), suited(kAce, 2), suited(kQueen, 0),
    suited(kTen, 0), suited(1, 0), suited(2, 1), suited(4, 3)};

constexpr std::array<Pattern, 6> kBbVsSmallAddedBluff{
    suited(kAce, 3), suited(kAce, 2), suited(kAce, 0),
    suited(kKing, 1), suited(5, 3), suited(4, 2)};

constexpr std::array<Pattern, 10> kBbVsSmallRemovedBluff{
    offsuit(kAce, 0), offsuit(kKing, 0), offsuit(kQueen, 0),
    offsuit(kKing, 1), offsuit(kQueen, 1), offsuit(kQueen, 2),
    offsuit(kJack, 3), offsuit(kTen, 3), offsuit(5, 3),
    offsuit(4, 2)};

/* Facing a three-bet: 6 to under 16 bb left to call. */
constexpr ChartMatrix kVsThreeBet{
    "VVVVCCCCCCCCC", "VVCBCCCCCCCCC", "VCVCCCCCCCCBB",
    "CCCVCCCCFFFFF", "CCCCVCCFFFFFF", "CFFCCCCCFFFFF",
    "CFFFFFCBCFFFF", "FFFFFFFCBFFFF", "FFFFFFFFCBFFF",
    "FFFFFFFFFCCFF", "FFFFFFFFFFCCC", "FFFFFFFFFFFCF",
    "FFFFFFFFFFFFC"};

/* Facing a four-bet: 16 to under 31 bb left to call. */
constexpr ChartMatrix kVsFourBet{
    "VVCCCCFFFBBFF", "VVCBBBFFBFFFF", "BFVCCFFFFFFFF",
    "FFFVCFFFFFFFF", "FFFFCCFFFFFFF", "FFFFFCCFFFFFF",
    "FFFFFFCCFFFFF", "FFFFFFFCCFFFF", "FFFFFFFFCCFFF",
    "FFFFFFFFFCCFF", "FFFFFFFFFFFFF", "FFFFFFFFFFFFF",
    "FFFFFFFFFFFFF"};

constexpr std::array<Pattern, 3> kSmallRaiseValue{
    pair(kAce), offsuit(kAce, kKing), pair(kKing)};

constexpr std::array<Pattern, 9> kSmallRaiseBluff{
    offsuit(kKing, 4), offsuit(kAce, 1), offsuit(kAce, 0),
    suited(kTen, 7), suited(5, 4), suited(4, 3), suited(3, 2),
    suited(kTen, 3), suited(6, 5)};

constexpr std::array<Pattern, 12> kSmallRaiseCall{
    offsuit(kAce, kJack), offsuit(kAce, kTen),
    offsuit(kKing, kJack), offsuit(kQueen, kTen),
    offsuit(kKing, kQueen),
    /* Aces below A9s call rather than raise. */
    suited(kAce, 6), suited(kAce, 5), suited(kAce, 4), suited(kAce, 3),
    suited(kAce, 2), suited(kAce, 1), suited(kAce, 0)};

constexpr std::array<Pattern, 17> kSmallRaiseFold{
    offsuit(kQueen, 5), offsuit(kKing, 3), offsuit(kKing, 2),
    offsuit(kQueen, 4), offsuit(kJack, 5), offsuit(kTen, 5),
    offsuit(4, 3), offsuit(kKing, 1), offsuit(kKing, 0),
    offsuit(kQueen, 3), offsuit(kQueen, 2), offsuit(kQueen, 1),
    offsuit(kQueen, 0), offsuit(kJack, 4), offsuit(kTen, 4),
    offsuit(7, 4), offsuit(6, 4)};

/* Facing a five-bet, and the same range answers an all-in-sized raise. */
constexpr std::array<Pattern, 5> kFiveBetJam{
    pair(kAce), pair(kKing), pair(kQueen), suited(kAce, kKing),
    offsuit(kAce, kKing)};

constexpr std::array<Pattern, 12> kFiveBetCall{
    pair(kJack), pair(kTen), suited(kAce, kQueen),
    suited(kAce, kJack), suited(kKing, kQueen), suited(4, 3),
    suited(5, 4), suited(6, 5), suited(7, 6), suited(kTen, 7),
    offsuit(kAce, kQueen), offsuit(kAce, kJack)};

bool valid_hole(FeltCard first, FeltCard second) {
  return first < 52U && second < 52U && first != second;
}

std::uint8_t rank_of(FeltCard card) {
  return static_cast<std::uint8_t>(card >> 2U);
}

std::uint8_t suit_of(FeltCard card) {
  return static_cast<std::uint8_t>(card & 3U);
}

bool matches(FeltCard first, FeltCard second, const Pattern& pattern) {
  const std::uint8_t first_rank = rank_of(first);
  const std::uint8_t second_rank = rank_of(second);
  const std::uint8_t high = std::max(first_rank, second_rank);
  const std::uint8_t low = std::min(first_rank, second_rank);
  if (high != pattern.high || low != pattern.low) {
    return false;
  }
  if (high == low) {
    return true;
  }
  return (suit_of(first) == suit_of(second)) == pattern.suited;
}

template <std::size_t Size>
bool matches_any(FeltCard first,
                 FeltCard second,
                 const std::array<Pattern, Size>& patterns) {
  for (const Pattern& pattern : patterns) {
    if (matches(first, second, pattern)) {
      return true;
    }
  }
  return false;
}

char matrix_code(const ChartMatrix& matrix, FeltCard first, FeltCard second) {
  const std::uint8_t first_rank = rank_of(first);
  const std::uint8_t second_rank = rank_of(second);
  const std::uint8_t high = std::max(first_rank, second_rank);
  const std::uint8_t low = std::min(first_rank, second_rank);
  const bool is_suited = suit_of(first) == suit_of(second);
  const std::uint8_t row_rank = high == low || is_suited ? high : low;
  const std::uint8_t column_rank = high == low || is_suited ? low : high;
  const std::size_t row = static_cast<std::size_t>(12U - row_rank);
  const std::size_t column = static_cast<std::size_t>(12U - column_rank);
  return matrix[row][column];
}

FeltPreflopChartAction action_from_code(char code) {
  switch (code) {
    case 'V':
      return FELT_PREFLOP_CHART_RAISE_VALUE;
    case 'B':
      return FELT_PREFLOP_CHART_RAISE_BLUFF;
    case 'C':
      return FELT_PREFLOP_CHART_PASSIVE;
    default:
      return FELT_PREFLOP_CHART_FOLD;
  }
}

FeltChips big_blind(const FeltGameState* state);

std::uint16_t facing_size_bb_x100(const FeltGameState* state) {
  const FeltChips bb = big_blind(state);
  if (bb <= 0 || state->opp_street_contribution <= 0 ||
      state->opp_street_contribution >
          std::numeric_limits<FeltChips>::max() / 100) {
    return 0U;
  }
  const FeltChips scaled = state->opp_street_contribution * 100 / bb;
  return static_cast<std::uint16_t>(std::min<FeltChips>(
      scaled, std::numeric_limits<std::uint16_t>::max()));
}

std::uint16_t call_size_bb_x100(const FeltGameState* state) {
  const FeltChips bb = big_blind(state);
  if (bb <= 0 || state->to_call <= 0 ||
      state->to_call > std::numeric_limits<FeltChips>::max() / 100) {
    return 0U;
  }
  const FeltChips scaled = state->to_call * 100 / bb;
  return static_cast<std::uint16_t>(std::min<FeltChips>(
      scaled, std::numeric_limits<std::uint16_t>::max()));
}

bool valid_state(const FeltGameState* state) {
  return state != nullptr && state->abi_version == FELT_BOT_ABI_VERSION &&
         state->struct_size >= sizeof(FeltGameState) &&
         state->street == FELT_STREET_PREFLOP &&
         (state->position == FELT_POSITION_BUTTON ||
          state->position == FELT_POSITION_BIG_BLIND) &&
         (state->history_count == 0U || state->history != nullptr) &&
         valid_hole(state->hole[0], state->hole[1]);
}

FeltPreflopSpot recognize_size_spot(const FeltGameState* state) {
  if (!valid_state(state)) {
    return FELT_PREFLOP_SPOT_INVALID;
  }
  std::uint32_t first_voluntary = 0;
  bool has_raise = false;
  for (std::uint32_t index = 0; index < state->history_count; ++index) {
    const FeltActionEvent& event = state->history[index];
    if (event.street != FELT_STREET_PREFLOP ||
        event.type == FELT_EVENT_POST_SMALL_BLIND ||
        event.type == FELT_EVENT_POST_BIG_BLIND) {
      continue;
    }
    if (first_voluntary == 0U) {
      first_voluntary = event.type;
    }
    if (event.type == FELT_EVENT_BET || event.type == FELT_EVENT_RAISE) {
      has_raise = true;
    }
  }

  if (has_raise) {
    const std::uint16_t call_size = call_size_bb_x100(state);
    if (call_size == 0U) {
      return FELT_PREFLOP_SPOT_INVALID;
    }
    if (call_size >= 5000U) {
      return FELT_PREFLOP_VS_ALL_IN_SIZED_RAISE;
    }
    if (call_size >= 3100U) {
      return FELT_PREFLOP_VS_FIVE_BET;
    }
    if (call_size >= 1600U) {
      return FELT_PREFLOP_VS_FOUR_BET;
    }
    if (call_size >= 600U) {
      return FELT_PREFLOP_VS_THREE_BET;
    }
    return state->position == FELT_POSITION_BIG_BLIND
               ? FELT_PREFLOP_BB_VS_SMALL_RAISE
               : FELT_PREFLOP_SB_VS_SMALL_RAISE;
  }

  if (state->position == FELT_POSITION_BUTTON) {
    if (first_voluntary == 0U) {
      return FELT_PREFLOP_SB_FIRST_IN;
    }
  } else if (state->position == FELT_POSITION_BIG_BLIND) {
    if (first_voluntary == FELT_EVENT_CALL) {
      return FELT_PREFLOP_BB_VS_SB_LIMP;
    }
  }
  return FELT_PREFLOP_SPOT_INVALID;
}

FeltPreflopSpot recognize_action_count_spot(const FeltGameState* state) {
  if (!valid_state(state)) {
    return FELT_PREFLOP_SPOT_INVALID;
  }

  std::uint32_t first_voluntary = 0U;
  std::uint32_t raise_count = 0U;
  for (std::uint32_t index = 0; index < state->history_count; ++index) {
    const FeltActionEvent& event = state->history[index];
    if (event.street != FELT_STREET_PREFLOP ||
        event.type == FELT_EVENT_POST_SMALL_BLIND ||
        event.type == FELT_EVENT_POST_BIG_BLIND) {
      continue;
    }
    if (first_voluntary == 0U) {
      first_voluntary = event.type;
    }
    if (event.type == FELT_EVENT_BET || event.type == FELT_EVENT_RAISE) {
      ++raise_count;
    }
  }

  if (raise_count == 0U) {
    if (state->position == FELT_POSITION_BUTTON && first_voluntary == 0U) {
      return FELT_PREFLOP_SB_FIRST_IN;
    }
    if (state->position == FELT_POSITION_BIG_BLIND &&
        first_voluntary == FELT_EVENT_CALL) {
      return FELT_PREFLOP_BB_VS_SB_LIMP;
    }
    return FELT_PREFLOP_SPOT_INVALID;
  }
  if (raise_count == 1U) {
    return state->position == FELT_POSITION_BIG_BLIND
               ? FELT_PREFLOP_BB_VS_SMALL_RAISE
               : FELT_PREFLOP_SB_VS_SMALL_RAISE;
  }
  if (raise_count == 2U) {
    return FELT_PREFLOP_VS_THREE_BET;
  }
  if (raise_count == 3U) {
    return FELT_PREFLOP_VS_FOUR_BET;
  }
  return FELT_PREFLOP_VS_FIVE_BET;
}

std::uint16_t scaled_raise_size(std::uint16_t facing_size,
                                std::uint32_t numerator,
                                std::uint32_t denominator,
                                std::uint16_t minimum) {
  const std::uint32_t scaled =
      static_cast<std::uint32_t>(facing_size) * numerator / denominator;
  return static_cast<std::uint16_t>(std::min<std::uint32_t>(
      std::max<std::uint32_t>(minimum, scaled),
      std::numeric_limits<std::uint16_t>::max()));
}

/* Voluntary preflop raises already made, by either player. */
std::uint32_t raises_so_far(const FeltGameState* state) {
  std::uint32_t count = 0U;
  for (std::uint32_t index = 0; index < state->history_count; ++index) {
    const FeltActionEvent& event = state->history[index];
    if (event.street != FELT_STREET_PREFLOP) {
      continue;
    }
    if (event.type == FELT_EVENT_BET || event.type == FELT_EVENT_RAISE) {
      ++count;
    }
  }
  return count;
}

/* Openers are a fixed number of big blinds; every re-raise is a multiple of
 * the raise in front of it, shrinking as the pot gets deeper. Our raise is
 * the three-bet when one raise has been made, the four-bet at two, and a
 * five-bet or beyond at three or more. raise_action clamps the result into
 * the legal range, so a multiple past the stack simply becomes an all-in. */
std::uint16_t raise_size_for(FeltPreflopSpot spot,
                             const FeltGameState* state) {
  const std::uint16_t facing_size = facing_size_bb_x100(state);
  switch (spot) {
    case FELT_PREFLOP_SB_FIRST_IN:
      return 250U;
    case FELT_PREFLOP_BB_VS_SB_LIMP:
      return 400U;
    case FELT_PREFLOP_BB_VS_SMALL_RAISE:
    case FELT_PREFLOP_SB_VS_SMALL_RAISE:
    case FELT_PREFLOP_VS_THREE_BET:
    case FELT_PREFLOP_VS_FOUR_BET:
      switch (raises_so_far(state)) {
        case 0U:
          return 250U;
        case 1U:
          return scaled_raise_size(facing_size, 7U, 2U, 0U);
        case 2U:
          return scaled_raise_size(facing_size, 3U, 1U, 0U);
        default:
          return scaled_raise_size(facing_size, 2U, 1U, 0U);
      }
    default:
      return 0U;
  }
}

FeltAction check_or_fold(const FeltGameState* state) {
  FeltAction action{};
  if (state != nullptr &&
      (state->legal_actions & FELT_LEGAL_CHECK) != 0U) {
    action.type = FELT_ACTION_CHECK;
  } else {
    action.type = FELT_ACTION_FOLD;
  }
  return action;
}

FeltAction passive_action(const FeltGameState* state) {
  FeltAction action{};
  if ((state->legal_actions & FELT_LEGAL_CALL) != 0U) {
    action.type = FELT_ACTION_CALL;
    return action;
  }
  return check_or_fold(state);
}

FeltChips big_blind(const FeltGameState* state) {
  for (std::uint32_t index = 0; index < state->history_count; ++index) {
    const FeltActionEvent& event = state->history[index];
    if (event.type == FELT_EVENT_POST_BIG_BLIND && event.amount_to > 0) {
      return event.amount_to;
    }
  }
  return 0;
}

FeltAction raise_action(const FeltGameState* state,
                        std::uint16_t raise_to_bb_x100,
                        bool call_if_raise_unavailable) {
  if ((state->legal_actions & FELT_LEGAL_RAISE_TO) == 0U) {
    return call_if_raise_unavailable ? passive_action(state)
                                     : check_or_fold(state);
  }

  FeltAction action{};
  action.type = FELT_ACTION_RAISE_TO;
  if (state->max_raise_to < state->min_raise_to) {
    action.amount_to = state->max_raise_to;
    return action;
  }

  const FeltChips bb = big_blind(state);
  if (bb <= 0 || raise_to_bb_x100 == 0U ||
      bb > std::numeric_limits<FeltChips>::max() / raise_to_bb_x100) {
    return check_or_fold(state);
  }
  const FeltChips target = bb * raise_to_bb_x100 / 100;
  action.amount_to = std::clamp(target, state->min_raise_to,
                               state->max_raise_to);
  return action;
}

FeltAction all_in_action(const FeltGameState* state) {
  if ((state->legal_actions & FELT_LEGAL_RAISE_TO) != 0U) {
    FeltAction action{};
    action.type = FELT_ACTION_RAISE_TO;
    action.amount_to = state->max_raise_to;
    return action;
  }
  return passive_action(state);
}

FeltPreflopDecision decision_for(const FeltGameState* state,
                                 FeltPreflopSpot spot) {
  FeltPreflopDecision decision{};
  decision.spot = spot;
  if (spot == FELT_PREFLOP_SPOT_INVALID) {
    return decision;
  }
  decision.chart_action = felt_preflop_baseline_lookup(
      spot, state->hole[0], state->hole[1]);
  decision.raise_to_bb_x100 = raise_size_for(spot, state);
  decision.valid = true;
  return decision;
}

FeltAction action_for(const FeltGameState* state,
                      FeltPreflopDecision decision,
                      bool bluff_calls_if_raise_unavailable) {
  if (!decision.valid) {
    return check_or_fold(state);
  }
  switch (decision.chart_action) {
    case FELT_PREFLOP_CHART_PASSIVE:
      return passive_action(state);
    case FELT_PREFLOP_CHART_RAISE_VALUE:
      return raise_action(state, decision.raise_to_bb_x100, true);
    case FELT_PREFLOP_CHART_RAISE_BLUFF:
      return raise_action(state, decision.raise_to_bb_x100,
                          bluff_calls_if_raise_unavailable);
    case FELT_PREFLOP_CHART_ALL_IN:
      return all_in_action(state);
    default: {
      FeltAction action{};
      if ((state->legal_actions & FELT_LEGAL_FOLD) != 0U) {
        action.type = FELT_ACTION_FOLD;
        return action;
      }
      return check_or_fold(state);
    }
  }
}

}  // namespace

extern "C" std::uint16_t felt_preflop_class(FeltCard first,
                                             FeltCard second) {
  if (!valid_hole(first, second)) {
    return FELT_INVALID_PREFLOP_CLASS;
  }
  const std::uint16_t first_rank = rank_of(first);
  const std::uint16_t second_rank = rank_of(second);
  const std::uint16_t high = std::max(first_rank, second_rank);
  const std::uint16_t low = std::min(first_rank, second_rank);
  if (high == low) {
    return static_cast<std::uint16_t>(high * kRankCount + low);
  }
  return suit_of(first) == suit_of(second)
             ? static_cast<std::uint16_t>(low * kRankCount + high)
             : static_cast<std::uint16_t>(high * kRankCount + low);
}

extern "C" FeltPreflopChartAction felt_preflop_baseline_lookup(
    FeltPreflopSpot spot, FeltCard first, FeltCard second) {
  if (!valid_hole(first, second)) {
    return FELT_PREFLOP_CHART_FOLD;
  }

  switch (spot) {
    case FELT_PREFLOP_SB_FIRST_IN:
      return action_from_code(matrix_code(kSmallBlindFirstIn, first, second));
    case FELT_PREFLOP_BB_VS_SMALL_RAISE:
      if (matches_any(first, second, kBbVsSmallAddedBluff)) {
        return FELT_PREFLOP_CHART_RAISE_BLUFF;
      }
      if (matches_any(first, second, kBbVsSmallRemovedBluff)) {
        return FELT_PREFLOP_CHART_PASSIVE;
      }
      return action_from_code(matrix_code(kBigBlindVsOpen, first, second));
    case FELT_PREFLOP_BB_VS_SB_LIMP: {
      if (matches_any(first, second, kBbVsLimpValuePairs)) {
        return FELT_PREFLOP_CHART_RAISE_VALUE;
      }
      if (matches_any(first, second, kBbVsLimpAddedBluff)) {
        return FELT_PREFLOP_CHART_RAISE_BLUFF;
      }
      const FeltPreflopChartAction open_action =
          action_from_code(matrix_code(kBigBlindVsOpen, first, second));
      if (open_action == FELT_PREFLOP_CHART_RAISE_VALUE) {
        return open_action;
      }
      return open_action == FELT_PREFLOP_CHART_RAISE_BLUFF &&
                     suit_of(first) == suit_of(second)
                 ? FELT_PREFLOP_CHART_RAISE_BLUFF
                 : FELT_PREFLOP_CHART_PASSIVE;
    }
    case FELT_PREFLOP_SB_VS_SMALL_RAISE:
      if (matches_any(first, second, kSmallRaiseValue)) {
        return FELT_PREFLOP_CHART_RAISE_VALUE;
      }
      if (matches_any(first, second, kSmallRaiseBluff)) {
        return FELT_PREFLOP_CHART_RAISE_BLUFF;
      }
      if (matches_any(first, second, kSmallRaiseCall)) {
        return FELT_PREFLOP_CHART_PASSIVE;
      }
      if (matches_any(first, second, kSmallRaiseFold)) {
        return FELT_PREFLOP_CHART_FOLD;
      }
      switch (action_from_code(
          matrix_code(kSmallBlindFirstIn, first, second))) {
        case FELT_PREFLOP_CHART_RAISE_VALUE:
          return FELT_PREFLOP_CHART_RAISE_VALUE;
        case FELT_PREFLOP_CHART_RAISE_BLUFF:
        case FELT_PREFLOP_CHART_PASSIVE:
          return FELT_PREFLOP_CHART_PASSIVE;
        default:
          return FELT_PREFLOP_CHART_FOLD;
      }
    case FELT_PREFLOP_VS_THREE_BET:
      return action_from_code(matrix_code(kVsThreeBet, first, second));
    case FELT_PREFLOP_VS_FOUR_BET:
      return action_from_code(matrix_code(kVsFourBet, first, second));
    case FELT_PREFLOP_VS_FIVE_BET:
      if (matches_any(first, second, kFiveBetJam)) {
        return FELT_PREFLOP_CHART_ALL_IN;
      }
      return matches_any(first, second, kFiveBetCall)
                 ? FELT_PREFLOP_CHART_PASSIVE
                 : FELT_PREFLOP_CHART_FOLD;
    case FELT_PREFLOP_VS_ALL_IN_SIZED_RAISE:
      /* A raise of 45 bb or more is treated as an all-in, but at 200 bb it
       * usually is not one: the answer is to put the rest in, not to call and
       * play three streets out of position. all_in_action falls back to a
       * call when raising is not legal, which is the case when the opponent
       * really is all-in. */
      return matches_any(first, second, kFiveBetJam)
                 ? FELT_PREFLOP_CHART_ALL_IN
                 : FELT_PREFLOP_CHART_FOLD;
    default:
      return FELT_PREFLOP_CHART_FOLD;
  }
}

extern "C" FeltPreflopDecision felt_preflop_baseline_decision(
    const FeltGameState* state) {
  return decision_for(state, recognize_size_spot(state));
}

extern "C" FeltAction felt_preflop_baseline_action(
    const FeltGameState* state) {
  return action_for(state, felt_preflop_baseline_decision(state), false);
}

extern "C" FeltPreflopDecision felt_preflop_action_count_v0_decision(
    const FeltGameState* state) {
  return decision_for(state, recognize_action_count_spot(state));
}

extern "C" FeltAction felt_preflop_action_count_v0_action(
    const FeltGameState* state) {
  return action_for(state, felt_preflop_action_count_v0_decision(state),
                    true);
}

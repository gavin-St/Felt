#include "felt/match.hpp"

#include "felt/random.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace felt {
namespace {

void checked_add(FeltChips& total, FeltChips value) {
  constexpr FeltChips minimum = std::numeric_limits<FeltChips>::min();
  constexpr FeltChips maximum = std::numeric_limits<FeltChips>::max();
  if ((value > 0 && total > maximum - value) ||
      (value < 0 && total < minimum - value)) {
    throw std::overflow_error("match chip total overflowed");
  }
  total += value;
}

}  // namespace

void validate_match_config(const MatchConfig& config) {
  if (config.hand_count == 0) {
    throw std::invalid_argument("hand count must be positive");
  }
  if (config.duplicate && (config.hand_count % 2U) != 0U) {
    throw std::invalid_argument(
        "hand count must be even when duplicate play is enabled");
  }
  if (config.starting_stack <= 0 || config.small_blind <= 0 ||
      config.small_blind >= config.big_blind ||
      config.big_blind > config.starting_stack) {
    throw std::invalid_argument(
        "match config requires 0 < small blind < big blind <= starting stack");
  }
}

HandCards deal_hand(std::uint64_t match_seed, std::uint64_t deal_index) {
  const Deck deck = shuffled_deck(match_seed, deal_index);
  HandCards cards;
  cards.hole[FELT_POSITION_BUTTON] = {deck[0], deck[2]};
  cards.hole[FELT_POSITION_BIG_BLIND] = {deck[1], deck[3]};
  for (std::size_t index = 0; index < cards.board.size(); ++index) {
    cards.board[index] = deck[index + 4U];
  }
  return cards;
}

MatchResult play_match(const MatchConfig& config,
                       BotRunner& bot_a,
                       BotRunner& bot_b,
                       MatchObserver* observer) {
  validate_match_config(config);
  std::array<BotRunner*, 2> bots_by_index{&bot_a, &bot_b};
  MatchResult match;
  HandCards duplicate_cards;

  for (std::uint64_t hand_index = 0; hand_index < config.hand_count;
       ++hand_index) {
    const std::uint64_t deal_index =
        config.duplicate ? hand_index / 2U : hand_index;
    const std::uint32_t button_bot =
        static_cast<std::uint32_t>(hand_index % 2U);
    const std::array<std::uint32_t, 2> bot_index_by_position{
        button_bot, 1U - button_bot};
    const std::array<BotRunner*, 2> positional_bots{
        bots_by_index[bot_index_by_position[FELT_POSITION_BUTTON]],
        bots_by_index[bot_index_by_position[FELT_POSITION_BIG_BLIND]]};

    HandConfig hand_config;
    hand_config.starting_stack = config.starting_stack;
    hand_config.small_blind = config.small_blind;
    hand_config.big_blind = config.big_blind;
    hand_config.decision_cap_us = config.decision_cap_us;
    hand_config.match_seed = config.match_seed;
    hand_config.randomness_index = deal_index;

    MatchHand hand;
    hand.hand_index = hand_index;
    hand.deal_index = deal_index;
    hand.bot_index_by_position = bot_index_by_position;
    if (!config.duplicate) {
      hand.cards = deal_hand(config.match_seed, deal_index);
    } else if ((hand_index % 2U) == 0U) {
      duplicate_cards = deal_hand(config.match_seed, deal_index);
      hand.cards = duplicate_cards;
    } else {
      hand.cards = duplicate_cards;
    }
    hand.result = play_hand(hand_config, hand.cards, positional_bots);

    for (std::size_t position = 0; position < 2; ++position) {
      const std::size_t bot_index = hand.bot_index_by_position[position];
      const FeltChips net = hand.result.raw_net[position];
      checked_add(match.net_by_bot[bot_index], net);
      checked_add(match.net_by_bot_and_position[bot_index][position], net);
    }
    ++match.hand_count;

    if (observer != nullptr) {
      observer->on_hand(hand);
    }
  }

  FeltChips global_net = match.net_by_bot[0];
  checked_add(global_net, match.net_by_bot[1]);
  if (global_net != 0) {
    throw std::logic_error("match result was not zero-sum");
  }
  for (std::size_t bot = 0; bot < 2; ++bot) {
    FeltChips position_net = match.net_by_bot_and_position[bot][0];
    checked_add(position_net, match.net_by_bot_and_position[bot][1]);
    if (position_net != match.net_by_bot[bot]) {
      throw std::logic_error("match position totals did not reconcile");
    }
  }
  return match;
}

}  // namespace felt

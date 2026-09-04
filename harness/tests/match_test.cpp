#include "felt/match.hpp"

#include "felt/random.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class CheckFoldBot final : public felt::BotRunner {
 public:
  std::string_view name() const noexcept override { return "check-fold"; }

  FeltAction act(const FeltGameState& state) override {
    return FeltAction{(state.legal_actions & FELT_LEGAL_CHECK) != 0U
                          ? FELT_ACTION_CHECK
                          : FELT_ACTION_FOLD,
                      0U, 0};
  }
};

class CheckCallBot final : public felt::BotRunner {
 public:
  std::string_view name() const noexcept override { return "check-call"; }

  FeltAction act(const FeltGameState& state) override {
    const std::uint32_t type =
        (state.legal_actions & FELT_LEGAL_CHECK) != 0U
            ? FELT_ACTION_CHECK
            : ((state.legal_actions & FELT_LEGAL_CALL) != 0U
                   ? FELT_ACTION_CALL
                   : FELT_ACTION_FOLD);
    return FeltAction{type, 0U, 0};
  }
};

class AlwaysAllInBot final : public felt::BotRunner {
 public:
  std::string_view name() const noexcept override { return "always-all-in"; }

  FeltAction act(const FeltGameState& state) override {
    if ((state.legal_actions & FELT_LEGAL_RAISE_TO) != 0U) {
      return FeltAction{FELT_ACTION_RAISE_TO, 0U, state.max_raise_to};
    }
    if ((state.legal_actions & FELT_LEGAL_CALL) != 0U) {
      return FeltAction{FELT_ACTION_CALL, 0U, 0};
    }
    if ((state.legal_actions & FELT_LEGAL_CHECK) != 0U) {
      return FeltAction{FELT_ACTION_CHECK, 0U, 0};
    }
    return FeltAction{FELT_ACTION_FOLD, 0U, 0};
  }
};

class CapturingObserver final : public felt::MatchObserver {
 public:
  void on_hand(const felt::MatchHand& hand) override {
    hands.push_back(hand);
  }

  std::vector<felt::MatchHand> hands;
};

void require_invalid_match(const felt::MatchConfig& config,
                           const std::string& message) {
  CheckFoldBot a;
  CheckFoldBot b;
  try {
    (void)felt::play_match(config, a, b);
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

void test_physical_deal_order() {
  constexpr std::uint64_t seed = 123;
  constexpr std::uint64_t deal_index = 7;
  const felt::Deck deck = felt::shuffled_deck(seed, deal_index);
  const felt::HandCards cards = felt::deal_hand(seed, deal_index);

  require(cards.hole[FELT_POSITION_BUTTON] ==
              std::array<felt::Card, 2>{deck[0], deck[2]},
          "button cards did not follow the physical deal order");
  require(cards.hole[FELT_POSITION_BIG_BLIND] ==
              std::array<felt::Card, 2>{deck[1], deck[3]},
          "big-blind cards did not follow the physical deal order");
  for (std::size_t index = 0; index < cards.board.size(); ++index) {
    require(cards.board[index] == deck[index + 4U],
            "board did not follow the physical deal order");
  }
}

void test_duplicate_cards_seats_and_randomness() {
  CheckCallBot a;
  CheckCallBot b;
  CapturingObserver observer;
  felt::MatchConfig config;
  config.hand_count = 4;
  config.match_seed = 991;
  const felt::MatchResult result = felt::play_match(config, a, b, &observer);

  require(observer.hands.size() == 4 && result.hand_count == 4,
          "duplicate match played the wrong number of hands");
  for (std::size_t first = 0; first < observer.hands.size(); first += 2) {
    const felt::MatchHand& hand_a = observer.hands[first];
    const felt::MatchHand& hand_b = observer.hands[first + 1U];
    require(hand_a.deal_index == first / 2U &&
                hand_b.deal_index == first / 2U,
            "duplicate pair used the wrong deal index");
    require(hand_a.cards.hole == hand_b.cards.hole &&
                hand_a.cards.board == hand_b.cards.board,
            "duplicate pair did not preserve positional cards");
    require(hand_a.bot_index_by_position ==
                std::array<std::uint32_t, 2>{0U, 1U} &&
                hand_b.bot_index_by_position ==
                    std::array<std::uint32_t, 2>{1U, 0U},
            "duplicate pair did not swap bot positions");
    require(hand_a.result.decisions.size() ==
                hand_b.result.decisions.size(),
            "identical bots produced different duplicate action counts");
    for (std::size_t decision = 0;
         decision < hand_a.result.decisions.size(); ++decision) {
      require(hand_a.result.decisions[decision].decision_random ==
                  hand_b.result.decisions[decision].decision_random,
              "duplicate pair did not share positional decision randomness");
    }
  }
  require(result.net_by_bot[0] == 0 && result.net_by_bot[1] == 0,
          "identical bots did not cancel across duplicate pairs");
}

void test_no_duplicate_fresh_deals_and_alternation() {
  CheckFoldBot a;
  CheckFoldBot b;
  CapturingObserver observer;
  felt::MatchConfig config;
  config.hand_count = 3;
  config.match_seed = 8128;
  config.duplicate = false;
  (void)felt::play_match(config, a, b, &observer);

  require(observer.hands.size() == 3, "nonduplicate match hand count was wrong");
  for (std::size_t hand = 0; hand < observer.hands.size(); ++hand) {
    require(observer.hands[hand].deal_index == hand,
            "nonduplicate hand did not use its hand index as deal index");
    const std::uint32_t expected_button =
        static_cast<std::uint32_t>(hand % 2U);
    require(observer.hands[hand]
                .bot_index_by_position[FELT_POSITION_BUTTON] ==
                expected_button,
            "button did not alternate without duplication");
    require(observer.hands[hand].result.decisions.front().my_stack == 19'950,
            "starting stacks were not reset between hands");
  }
  require(observer.hands[0].cards.hole != observer.hands[1].cards.hole ||
              observer.hands[0].cards.board != observer.hands[1].cards.board,
          "successive nonduplicate hands reused the same deal");
}

void test_all_in_check_fold_oracle() {
  AlwaysAllInBot a;
  CheckFoldBot b;
  CapturingObserver observer;
  felt::MatchConfig config;
  config.hand_count = 2;
  config.match_seed = 17;
  const felt::MatchResult result = felt::play_match(config, a, b, &observer);

  require(result.net_by_bot == std::array<FeltChips, 2>{150, -150},
          "always-all-in versus check-fold payoff was wrong");
  require(result.net_by_bot_and_position[0] ==
              std::array<FeltChips, 2>{100, 50},
          "all-in bot position totals were wrong");
  require(result.net_by_bot_and_position[1] ==
              std::array<FeltChips, 2>{-50, -100},
          "check-fold bot position totals were wrong");
  require(observer.hands[0].result.reason == felt::HandEndReason::fold &&
              observer.hands[1].result.reason == felt::HandEndReason::fold,
          "betting oracle unexpectedly reached showdown");
}

void test_symmetric_all_ins_cancel() {
  AlwaysAllInBot a;
  AlwaysAllInBot b;
  felt::MatchConfig config;
  config.hand_count = 20;
  config.match_seed = 12345;
  const felt::MatchResult result = felt::play_match(config, a, b);
  require(result.net_by_bot[0] == 0 && result.net_by_bot[1] == 0,
          "symmetric all-ins did not cancel under duplicate play");
}

void test_match_validation() {
  felt::MatchConfig config;
  config.hand_count = 3;
  require_invalid_match(config, "odd duplicate hand count was accepted");

  config.hand_count = 0;
  require_invalid_match(config, "zero hand count was accepted");

  config.hand_count = 2;
  config.small_blind = config.big_blind;
  require_invalid_match(config, "invalid blind relationship was accepted");

  config.small_blind = 50;
  config.starting_stack = 99;
  require_invalid_match(config, "big blind above stack was accepted");
}

}  // namespace

int main() {
  try {
    test_physical_deal_order();
    test_duplicate_cards_seats_and_randomness();
    test_no_duplicate_fresh_deals_and_alternation();
    test_all_in_check_fold_oracle();
    test_symmetric_all_ins_cancel();
    test_match_validation();
  } catch (const std::exception& error) {
    std::cerr << "match_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

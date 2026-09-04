#include "felt/card.hpp"
#include "felt/hand_engine.hpp"
#include "felt/random.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

FeltAction action(std::uint32_t type, FeltChips amount_to = 0) {
  return FeltAction{type, 0U, amount_to};
}

struct StateSnapshot {
  FeltGameState state{};
  std::vector<FeltActionEvent> history;
};

class ScriptedBot final : public felt::BotRunner {
 public:
  explicit ScriptedBot(std::vector<FeltAction> actions)
      : actions_(std::move(actions)) {}

  std::string_view name() const noexcept override { return "scripted"; }

  FeltAction act(const FeltGameState& state) override {
    StateSnapshot snapshot;
    snapshot.state = state;
    snapshot.history.assign(state.history, state.history + state.history_count);
    snapshot.state.history = nullptr;
    states.push_back(std::move(snapshot));
    if (next_action_ == actions_.size()) {
      throw std::runtime_error("scripted bot ran out of actions");
    }
    return actions_[next_action_++];
  }

  [[nodiscard]] bool consumed_all_actions() const noexcept {
    return next_action_ == actions_.size();
  }

  std::vector<StateSnapshot> states;

 private:
  std::vector<FeltAction> actions_;
  std::size_t next_action_{};
};

std::uint64_t mix(std::uint64_t value) {
  value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31U);
}

class LegalRandomBot final : public felt::BotRunner {
 public:
  std::string_view name() const noexcept override { return "legal-random"; }

  FeltAction act(const FeltGameState& state) override {
    require(state.pot >= 0 && state.my_stack >= 0 && state.opp_stack >= 0 &&
                state.to_call >= 0,
            "random test observed negative public chips");
    for (std::size_t index = 0; index < 5; ++index) {
      const bool visible = index < state.board_count;
      require(visible ? state.board[index] != FELT_INVALID_CARD
                      : state.board[index] == FELT_INVALID_CARD,
              "random test observed an invalid board prefix");
    }

    std::array<std::uint32_t, 4> choices{};
    std::size_t count = 0;
    if ((state.legal_actions & FELT_LEGAL_FOLD) != 0U) {
      choices[count++] = FELT_ACTION_FOLD;
    }
    if ((state.legal_actions & FELT_LEGAL_CHECK) != 0U) {
      choices[count++] = FELT_ACTION_CHECK;
    }
    if ((state.legal_actions & FELT_LEGAL_CALL) != 0U) {
      choices[count++] = FELT_ACTION_CALL;
    }
    if ((state.legal_actions & FELT_LEGAL_RAISE_TO) != 0U) {
      choices[count++] = FELT_ACTION_RAISE_TO;
    }
    require(count != 0, "random test observed no legal action");

    std::uint64_t random = mix(state.decision_random);
    FeltAction chosen = action(choices[random % count]);
    if (chosen.type == FELT_ACTION_RAISE_TO) {
      if (state.max_raise_to < state.min_raise_to) {
        chosen.amount_to = state.max_raise_to;
      } else {
        random = mix(random);
        const std::uint64_t span =
            static_cast<std::uint64_t>(state.max_raise_to -
                                       state.min_raise_to) +
            1U;
        chosen.amount_to = state.min_raise_to +
                           static_cast<FeltChips>(random % span);
      }
    }
    return chosen;
  }
};

felt::HandCards make_cards(
    const std::array<std::string_view, 2>& button,
    const std::array<std::string_view, 2>& big_blind,
    const std::array<std::string_view, 5>& board) {
  felt::HandCards cards;
  for (std::size_t index = 0; index < 2; ++index) {
    cards.hole[FELT_POSITION_BUTTON][index] =
        felt::card_from_string(button[index]);
    cards.hole[FELT_POSITION_BIG_BLIND][index] =
        felt::card_from_string(big_blind[index]);
  }
  for (std::size_t index = 0; index < board.size(); ++index) {
    cards.board[index] = felt::card_from_string(board[index]);
  }
  return cards;
}

felt::HandCards winning_button_cards() {
  return make_cards({"Ac", "Ad"}, {"Kc", "Kd"},
                    {"2s", "3d", "7h", "8s", "9c"});
}

felt::HandCards chopped_cards() {
  return make_cards({"2c", "3d"}, {"4s", "5c"},
                    {"Th", "Jh", "Qh", "Kh", "Ah"});
}

felt::HandResult run_hand(ScriptedBot& button,
                          ScriptedBot& big_blind,
                          const felt::HandCards& cards =
                              winning_button_cards()) {
  const std::array<felt::BotRunner*, 2> bots{&button, &big_blind};
  felt::HandResult result = felt::play_hand(felt::HandConfig{}, cards, bots);
  require(button.consumed_all_actions(),
          "button script had unconsumed actions");
  require(big_blind.consumed_all_actions(),
          "big-blind script had unconsumed actions");
  require(result.raw_net[0] + result.raw_net[1] == 0,
          "hand result was not zero-sum");
  require(result.raw_payout[0] + result.raw_payout[1] ==
              result.committed[0] + result.committed[1],
          "payout did not reconcile with committed chips");
  return result;
}

void test_button_fold() {
  ScriptedBot button({action(FELT_ACTION_FOLD)});
  ScriptedBot big_blind({});
  const felt::HandResult result = run_hand(button, big_blind);

  require(result.reason == felt::HandEndReason::fold,
          "button fold did not end by fold");
  require(result.folded_position == FELT_POSITION_BUTTON,
          "wrong folded position");
  require(result.raw_net[0] == -50 && result.raw_net[1] == 50,
          "blind-fold payoff was wrong");
  require(result.events.size() == 3, "blind-fold history size was wrong");
  require(button.states[0].history.size() == 2,
          "forced blind posts were not visible in history");
  require(button.states[0].history[0].type == FELT_EVENT_POST_SMALL_BLIND &&
              button.states[0].history[1].type == FELT_EVENT_POST_BIG_BLIND,
          "blind history events were wrong");
}

void test_limp_bb_option_and_checkdown() {
  ScriptedBot button({action(FELT_ACTION_CALL), action(FELT_ACTION_CHECK),
                      action(FELT_ACTION_CHECK), action(FELT_ACTION_CHECK)});
  ScriptedBot big_blind({action(FELT_ACTION_CHECK),
                         action(FELT_ACTION_CHECK),
                         action(FELT_ACTION_CHECK),
                         action(FELT_ACTION_CHECK)});
  const felt::HandResult result = run_hand(button, big_blind);

  require(result.reason == felt::HandEndReason::showdown,
          "checkdown did not reach showdown");
  require(big_blind.states[0].state.street == FELT_STREET_PREFLOP &&
              big_blind.states[0].state.to_call == 0,
          "BB did not receive its preflop option");
  require((big_blind.states[0].state.legal_actions & FELT_LEGAL_CHECK) != 0U &&
              (big_blind.states[0].state.legal_actions &
               FELT_LEGAL_RAISE_TO) != 0U,
          "BB preflop option had the wrong legal actions");
  require(big_blind.states[0].state.min_raise_to == 200 &&
              big_blind.states[0].state.max_raise_to == 20'000,
          "BB preflop raise bounds were wrong");

  const std::array<std::uint8_t, 4> expected_board_counts{0, 3, 4, 5};
  for (std::size_t index = 0; index < expected_board_counts.size(); ++index) {
    require(button.states[index].state.board_count ==
                expected_board_counts[index],
            "button saw the wrong board prefix");
    require(big_blind.states[index].state.board_count ==
                expected_board_counts[index],
            "big blind saw the wrong board prefix");
  }
  require(result.committed[0] == 100 && result.committed[1] == 100,
          "checkdown commitments were wrong");
  require(result.raw_net[0] == 100 && result.raw_net[1] == -100,
          "showdown winner was wrong");
}

void test_bet_raise_reraise_all_in() {
  ScriptedBot button({action(FELT_ACTION_CALL),
                      action(FELT_ACTION_RAISE_TO, 300),
                      action(FELT_ACTION_RAISE_TO, 19'900)});
  ScriptedBot big_blind({action(FELT_ACTION_CHECK),
                         action(FELT_ACTION_RAISE_TO, 100),
                         action(FELT_ACTION_RAISE_TO, 700),
                         action(FELT_ACTION_CALL)});
  const felt::HandResult result = run_hand(button, big_blind);

  const std::array<std::uint32_t, 9> expected_types{
      FELT_EVENT_POST_SMALL_BLIND, FELT_EVENT_POST_BIG_BLIND,
      FELT_EVENT_CALL,             FELT_EVENT_CHECK,
      FELT_EVENT_BET,              FELT_EVENT_RAISE,
      FELT_EVENT_RAISE,            FELT_EVENT_RAISE,
      FELT_EVENT_CALL};
  require(result.events.size() == expected_types.size(),
          "all-in action history size was wrong");
  for (std::size_t index = 0; index < expected_types.size(); ++index) {
    require(result.events[index].type == expected_types[index],
            "all-in action history type was wrong");
  }
  require(result.reason == felt::HandEndReason::showdown &&
              result.ending_street == FELT_STREET_FLOP,
          "flop all-in did not run out to showdown");
  require(result.committed[0] == 20'000 &&
              result.committed[1] == 20'000,
          "all-in commitments were wrong");
  require(result.raw_net[0] == 20'000 && result.raw_net[1] == -20'000,
          "all-in payoff was wrong");
}

void test_short_all_in() {
  ScriptedBot button({action(FELT_ACTION_RAISE_TO, 15'000),
                      action(FELT_ACTION_CALL)});
  ScriptedBot big_blind({action(FELT_ACTION_RAISE_TO, 20'000)});
  const felt::HandResult result = run_hand(button, big_blind);

  require(big_blind.states[0].state.min_raise_to == 29'900 &&
              big_blind.states[0].state.max_raise_to == 20'000,
          "short all-in bounds were not represented by max < min");
  require(result.decisions[1].violation == felt::ActionViolation::none,
          "legal short all-in was rejected");
  require(result.committed[0] == 20'000 &&
              result.committed[1] == 20'000,
          "short all-in call did not reconcile");
}

void test_all_in_big_blind_is_not_asked_to_act() {
  ScriptedBot button({action(FELT_ACTION_CALL)});
  ScriptedBot big_blind({});
  const std::array<felt::BotRunner*, 2> bots{&button, &big_blind};
  felt::HandConfig config;
  config.starting_stack = 100;
  const felt::HandResult result =
      felt::play_hand(config, winning_button_cards(), bots);

  require(button.consumed_all_actions() && big_blind.consumed_all_actions(),
          "all-in blind hand consumed the wrong actions");
  require(big_blind.states.empty(), "all-in big blind was asked to act");
  require(result.reason == felt::HandEndReason::showdown &&
              result.committed[0] == 100 && result.committed[1] == 100,
          "all-in blind did not run out correctly");
  require(result.raw_net[0] + result.raw_net[1] == 0,
          "all-in blind result was not zero-sum");
}

void test_action_not_reopened() {
  ScriptedBot button({action(FELT_ACTION_RAISE_TO, 15'000),
                      action(FELT_ACTION_RAISE_TO, 20'000)});
  ScriptedBot big_blind({action(FELT_ACTION_RAISE_TO, 20'000)});
  const felt::HandResult result = run_hand(button, big_blind);

  require(button.states[1].state.legal_actions ==
              (FELT_LEGAL_FOLD | FELT_LEGAL_CALL),
          "raise was offered after a short all-in");
  require(button.states[1].state.min_raise_to == 0 &&
              button.states[1].state.max_raise_to == 0,
          "disabled raise bounds were not zero");
  require(result.decisions[2].violation ==
              felt::ActionViolation::illegal_action &&
              result.decisions[2].applied.type == FELT_ACTION_FOLD,
          "attempted non-reopened raise did not default to fold");
  require(result.raw_net[0] == -15'000 && result.raw_net[1] == 15'000,
          "short-all-in fold payoff was wrong");
}

void test_fold_on_each_street() {
  {
    ScriptedBot button({action(FELT_ACTION_CALL), action(FELT_ACTION_FOLD)});
    ScriptedBot big_blind({action(FELT_ACTION_CHECK),
                           action(FELT_ACTION_RAISE_TO, 100)});
    require(run_hand(button, big_blind).ending_street == FELT_STREET_FLOP,
            "flop fold ended on the wrong street");
  }
  {
    ScriptedBot button({action(FELT_ACTION_CALL), action(FELT_ACTION_CHECK),
                        action(FELT_ACTION_FOLD)});
    ScriptedBot big_blind({action(FELT_ACTION_CHECK),
                           action(FELT_ACTION_CHECK),
                           action(FELT_ACTION_RAISE_TO, 100)});
    require(run_hand(button, big_blind).ending_street == FELT_STREET_TURN,
            "turn fold ended on the wrong street");
  }
  {
    ScriptedBot button({action(FELT_ACTION_CALL), action(FELT_ACTION_CHECK),
                        action(FELT_ACTION_CHECK), action(FELT_ACTION_FOLD)});
    ScriptedBot big_blind({action(FELT_ACTION_CHECK),
                           action(FELT_ACTION_CHECK),
                           action(FELT_ACTION_CHECK),
                           action(FELT_ACTION_RAISE_TO, 100)});
    require(run_hand(button, big_blind).ending_street == FELT_STREET_RIVER,
            "river fold ended on the wrong street");
  }
}

void test_chopped_showdown() {
  ScriptedBot button({action(FELT_ACTION_CALL), action(FELT_ACTION_CHECK),
                      action(FELT_ACTION_CHECK), action(FELT_ACTION_CHECK)});
  ScriptedBot big_blind({action(FELT_ACTION_CHECK),
                         action(FELT_ACTION_CHECK),
                         action(FELT_ACTION_CHECK),
                         action(FELT_ACTION_CHECK)});
  const felt::HandResult result =
      run_hand(button, big_blind, chopped_cards());
  require(result.showdown_rank[0] == result.showdown_rank[1],
          "board-play hand did not chop");
  require(result.raw_payout[0] == 100 && result.raw_payout[1] == 100 &&
              result.raw_net[0] == 0 && result.raw_net[1] == 0,
          "chopped payout was wrong");
}

void test_illegal_action_defaults_and_effective_stack() {
  {
    ScriptedBot button({action(FELT_ACTION_RAISE_TO, 20'001)});
    ScriptedBot big_blind({});
    const felt::HandResult result = run_hand(button, big_blind);
    require(button.states[0].state.max_raise_to == 20'000,
            "effective-stack maximum was wrong");
    require(result.decisions[0].violation ==
                felt::ActionViolation::invalid_raise_amount &&
                result.decisions[0].applied.type == FELT_ACTION_FOLD,
            "oversized raise did not default to fold");
  }
  {
    ScriptedBot button({action(FELT_ACTION_CALL), action(FELT_ACTION_CHECK),
                        action(FELT_ACTION_CHECK),
                        action(FELT_ACTION_CHECK)});
    ScriptedBot big_blind({action(FELT_ACTION_FOLD),
                           action(FELT_ACTION_CHECK),
                           action(FELT_ACTION_CHECK),
                           action(FELT_ACTION_CHECK)});
    const felt::HandResult result = run_hand(button, big_blind);
    require(result.decisions[1].violation ==
                felt::ActionViolation::illegal_action &&
                result.decisions[1].applied.type == FELT_ACTION_CHECK,
            "illegal fold did not default to check");
    require(result.reason == felt::HandEndReason::showdown,
            "default check did not continue the hand");
  }
}

void test_invalid_inputs() {
  ScriptedBot button({});
  ScriptedBot big_blind({});
  const std::array<felt::BotRunner*, 2> bots{&button, &big_blind};

  try {
    felt::HandConfig invalid;
    invalid.small_blind = invalid.big_blind;
    (void)felt::play_hand(invalid, winning_button_cards(), bots);
    throw std::runtime_error("invalid blind configuration was accepted");
  } catch (const std::invalid_argument&) {
  }

  try {
    felt::HandCards invalid = winning_button_cards();
    invalid.board[0] = invalid.hole[0][0];
    (void)felt::play_hand(felt::HandConfig{}, invalid, bots);
    throw std::runtime_error("duplicate cards were accepted");
  } catch (const std::invalid_argument&) {
  }
}

void test_random_legal_hands() {
  LegalRandomBot button;
  LegalRandomBot big_blind;
  const std::array<felt::BotRunner*, 2> bots{&button, &big_blind};

  for (std::uint64_t seed = 1; seed <= 10'000; ++seed) {
    const felt::Deck deck = felt::shuffled_deck(seed, 0);
    felt::HandCards cards;
    cards.hole[FELT_POSITION_BUTTON] = {deck[0], deck[2]};
    cards.hole[FELT_POSITION_BIG_BLIND] = {deck[1], deck[3]};
    std::copy_n(deck.begin() + 4, 5, cards.board.begin());

    felt::HandConfig config;
    config.starting_stack =
        100 + static_cast<FeltChips>(mix(seed) % 19'901U);
    config.match_seed = seed;
    config.randomness_index = seed;
    const felt::HandResult result = felt::play_hand(config, cards, bots);

    require(result.raw_net[0] + result.raw_net[1] == 0,
            "random hand was not zero-sum");
    require(result.raw_payout[0] + result.raw_payout[1] ==
                result.committed[0] + result.committed[1],
            "random hand payout did not reconcile");
    for (std::size_t position = 0; position < 2; ++position) {
      require(result.committed[position] >= 0 &&
                  result.committed[position] <= config.starting_stack,
              "random hand exceeded an effective stack");
    }
    for (const felt::DecisionRecord& decision : result.decisions) {
      require(decision.violation == felt::ActionViolation::none,
              "engine rejected a generated legal action");
    }
  }
}

}  // namespace

int main() {
  try {
    test_button_fold();
    test_limp_bb_option_and_checkdown();
    test_bet_raise_reraise_all_in();
    test_short_all_in();
    test_all_in_big_blind_is_not_asked_to_act();
    test_action_not_reopened();
    test_fold_on_each_street();
    test_chopped_showdown();
    test_illegal_action_defaults_and_effective_stack();
    test_invalid_inputs();
    test_random_legal_hands();
  } catch (const std::exception& error) {
    std::cerr << "hand_engine_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

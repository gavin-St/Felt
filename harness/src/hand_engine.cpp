#include "felt/hand_engine.hpp"

#include "felt/evaluator.hpp"
#include "felt/random.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace felt {
namespace {

constexpr std::size_t kButton = FELT_POSITION_BUTTON;
constexpr std::size_t kBigBlind = FELT_POSITION_BIG_BLIND;

struct EngineState {
  EngineState(const HandConfig& hand_config, const HandCards& hand_cards)
      : config(hand_config), cards(hand_cards) {}

  const HandConfig& config;
  const HandCards& cards;
  std::array<FeltChips, 2> stack{};
  std::array<FeltChips, 2> committed{};
  std::array<FeltChips, 2> street_contribution{};
  std::array<bool, 2> acted_since_full_raise{};
  FeltChips pot{};
  FeltChips last_full_raise_size{};
  std::uint32_t street{FELT_STREET_PREFLOP};
  std::uint32_t actor{FELT_POSITION_BUTTON};
  std::uint64_t decision_index{};
  HandResult result;
};

void validate_config(const HandConfig& config) {
  if (config.starting_stack <= 0 || config.small_blind <= 0 ||
      config.small_blind >= config.big_blind ||
      config.big_blind > config.starting_stack) {
    throw std::invalid_argument(
        "hand config requires 0 < small blind < big blind <= starting stack");
  }
}

void validate_cards(const HandCards& cards) {
  std::array<bool, kCardCount> seen{};
  for (const auto& hole : cards.hole) {
    for (const Card card : hole) {
      if (!is_valid_card(card) || seen[card]) {
        throw std::invalid_argument(
            "hand cards must contain nine distinct valid cards");
      }
      seen[card] = true;
    }
  }
  for (const Card card : cards.board) {
    if (!is_valid_card(card) || seen[card]) {
      throw std::invalid_argument(
          "hand cards must contain nine distinct valid cards");
    }
    seen[card] = true;
  }
}

void append_event(EngineState& engine,
                  std::uint32_t position,
                  std::uint32_t type,
                  FeltChips amount_to) {
  engine.result.events.push_back(
      FeltActionEvent{position, engine.street, type, 0U, amount_to});
}

void post_blind(EngineState& engine,
                std::size_t position,
                FeltChips amount,
                std::uint32_t event_type) {
  engine.stack[position] -= amount;
  engine.committed[position] += amount;
  engine.street_contribution[position] += amount;
  engine.pot += amount;
  append_event(engine, static_cast<std::uint32_t>(position), event_type,
               engine.street_contribution[position]);
}

std::uint8_t board_count_for_street(std::uint32_t street) {
  switch (street) {
    case FELT_STREET_PREFLOP:
      return 0;
    case FELT_STREET_FLOP:
      return 3;
    case FELT_STREET_TURN:
      return 4;
    case FELT_STREET_RIVER:
      return 5;
    default:
      throw std::logic_error("invalid engine street");
  }
}

FeltGameState make_game_state(const EngineState& engine) {
  const std::size_t actor = engine.actor;
  const std::size_t opponent = 1U - actor;
  const FeltChips highest = std::max(engine.street_contribution[0],
                                     engine.street_contribution[1]);
  const FeltChips uncapped_to_call =
      highest - engine.street_contribution[actor];
  const FeltChips to_call = std::min(uncapped_to_call, engine.stack[actor]);

  FeltGameState state{};
  state.abi_version = FELT_BOT_ABI_VERSION;
  state.struct_size = sizeof(FeltGameState);
  state.hole[0] = engine.cards.hole[actor][0];
  state.hole[1] = engine.cards.hole[actor][1];
  std::fill(std::begin(state.board), std::end(state.board), FELT_INVALID_CARD);
  state.board_count = board_count_for_street(engine.street);
  std::copy_n(engine.cards.board.begin(), state.board_count, state.board);
  state.street = engine.street;
  state.position = engine.actor;

  if (to_call == 0) {
    state.legal_actions |= FELT_LEGAL_CHECK;
  } else {
    state.legal_actions |= FELT_LEGAL_FOLD | FELT_LEGAL_CALL;
  }

  const FeltChips all_in_to =
      engine.street_contribution[actor] + engine.stack[actor];
  const bool can_raise = !engine.acted_since_full_raise[actor] &&
                         engine.stack[opponent] > 0 &&
                         engine.stack[actor] > to_call &&
                         all_in_to > highest;
  if (can_raise) {
    state.legal_actions |= FELT_LEGAL_RAISE_TO;
    state.min_raise_to =
        highest == 0 ? engine.config.big_blind
                     : highest + engine.last_full_raise_size;
    state.max_raise_to = all_in_to;
  }

  state.pot = engine.pot;
  state.my_stack = engine.stack[actor];
  state.opp_stack = engine.stack[opponent];
  state.my_street_contribution = engine.street_contribution[actor];
  state.opp_street_contribution = engine.street_contribution[opponent];
  state.to_call = to_call;
  state.decision_cap_us = engine.config.decision_cap_us;
  state.decision_random = felt::decision_random(
      engine.config.match_seed, engine.config.randomness_index,
      engine.decision_index, engine.actor);
  state.history = engine.result.events.data();
  state.history_count =
      static_cast<std::uint32_t>(engine.result.events.size());
  return state;
}

bool has_legal_action(const FeltGameState& state, std::uint32_t action) {
  switch (action) {
    case FELT_ACTION_FOLD:
      return (state.legal_actions & FELT_LEGAL_FOLD) != 0U;
    case FELT_ACTION_CHECK:
      return (state.legal_actions & FELT_LEGAL_CHECK) != 0U;
    case FELT_ACTION_CALL:
      return (state.legal_actions & FELT_LEGAL_CALL) != 0U;
    case FELT_ACTION_RAISE_TO:
      return (state.legal_actions & FELT_LEGAL_RAISE_TO) != 0U;
    default:
      return false;
  }
}

FeltAction default_action(const FeltGameState& state) {
  if ((state.legal_actions & FELT_LEGAL_CHECK) != 0U) {
    return FeltAction{FELT_ACTION_CHECK, 0U, 0};
  }
  return FeltAction{FELT_ACTION_FOLD, 0U, 0};
}

std::pair<FeltAction, ActionViolation> validate_action(
    const FeltGameState& state,
    FeltAction requested) {
  if (requested.reserved != 0U) {
    return {default_action(state), ActionViolation::nonzero_reserved};
  }
  if (requested.type < FELT_ACTION_FOLD ||
      requested.type > FELT_ACTION_RAISE_TO) {
    return {default_action(state), ActionViolation::unknown_action};
  }
  if (!has_legal_action(state, requested.type)) {
    return {default_action(state), ActionViolation::illegal_action};
  }
  if (requested.type == FELT_ACTION_RAISE_TO) {
    const bool short_all_in = state.max_raise_to < state.min_raise_to;
    const bool valid_amount =
        short_all_in
            ? requested.amount_to == state.max_raise_to
            : requested.amount_to >= state.min_raise_to &&
                  requested.amount_to <= state.max_raise_to;
    if (!valid_amount) {
      return {default_action(state), ActionViolation::invalid_raise_amount};
    }
    return {requested, ActionViolation::none};
  }
  requested.amount_to = 0;
  return {requested, ActionViolation::none};
}

DecisionRecord make_decision_record(const FeltGameState& state,
                                    FeltAction requested,
                                    FeltAction applied,
                                    ActionViolation violation) {
  return DecisionRecord{
      state.position,
      state.street,
      state.legal_actions,
      state.pot,
      state.my_stack,
      state.opp_stack,
      state.my_street_contribution,
      state.opp_street_contribution,
      state.to_call,
      state.min_raise_to,
      state.max_raise_to,
      state.decision_random,
      requested,
      applied,
      violation,
  };
}

void commit_chips(EngineState& engine, std::size_t actor, FeltChips amount) {
  if (amount < 0 || amount > engine.stack[actor]) {
    throw std::logic_error("engine attempted an invalid chip commitment");
  }
  engine.stack[actor] -= amount;
  engine.committed[actor] += amount;
  engine.street_contribution[actor] += amount;
  engine.pot += amount;
}

void finish_fold(EngineState& engine, std::size_t folded) {
  const std::size_t winner = 1U - folded;
  engine.result.reason = HandEndReason::fold;
  engine.result.ending_street = engine.street;
  engine.result.folded_position = static_cast<std::uint32_t>(folded);
  engine.result.committed = engine.committed;
  engine.result.raw_payout[winner] = engine.pot;
  for (std::size_t position = 0; position < 2; ++position) {
    engine.result.raw_net[position] =
        engine.result.raw_payout[position] - engine.committed[position];
  }
}

void finish_showdown(EngineState& engine) {
  engine.result.reason = HandEndReason::showdown;
  engine.result.ending_street = engine.street;
  engine.result.committed = engine.committed;

  for (std::size_t position = 0; position < 2; ++position) {
    std::array<Card, 7> seven{};
    seven[0] = engine.cards.hole[position][0];
    seven[1] = engine.cards.hole[position][1];
    std::copy(engine.cards.board.begin(), engine.cards.board.end(),
              seven.begin() + 2);
    engine.result.showdown_rank[position] = evaluate7_unchecked(seven);
  }

  if (engine.result.showdown_rank[kButton] >
      engine.result.showdown_rank[kBigBlind]) {
    engine.result.raw_payout[kButton] = engine.pot;
  } else if (engine.result.showdown_rank[kBigBlind] >
             engine.result.showdown_rank[kButton]) {
    engine.result.raw_payout[kBigBlind] = engine.pot;
  } else {
    engine.result.raw_payout[kButton] = engine.pot / 2;
    engine.result.raw_payout[kBigBlind] =
        engine.pot - engine.result.raw_payout[kButton];
  }

  for (std::size_t position = 0; position < 2; ++position) {
    engine.result.raw_net[position] =
        engine.result.raw_payout[position] - engine.committed[position];
  }
}

bool betting_round_complete(const EngineState& engine) {
  return engine.street_contribution[0] == engine.street_contribution[1] &&
         engine.acted_since_full_raise[0] &&
         engine.acted_since_full_raise[1];
}

void start_next_street(EngineState& engine) {
  ++engine.street;
  engine.actor = FELT_POSITION_BIG_BLIND;
  engine.street_contribution = {0, 0};
  engine.acted_since_full_raise = {false, false};
  engine.last_full_raise_size = engine.config.big_blind;
}

}  // namespace

HandResult play_hand(const HandConfig& config,
                     const HandCards& cards,
                     const std::array<BotRunner*, 2>& positional_bots) {
  validate_config(config);
  validate_cards(cards);
  if (positional_bots[0] == nullptr || positional_bots[1] == nullptr) {
    throw std::invalid_argument("play_hand requires two bot runners");
  }

  EngineState engine(config, cards);
  engine.stack = {config.starting_stack, config.starting_stack};
  engine.last_full_raise_size = config.big_blind;
  engine.result.events.reserve(256);
  engine.result.decisions.reserve(256);

  post_blind(engine, kButton, config.small_blind,
             FELT_EVENT_POST_SMALL_BLIND);
  post_blind(engine, kBigBlind, config.big_blind,
             FELT_EVENT_POST_BIG_BLIND);

  for (;;) {
    const FeltGameState state = make_game_state(engine);
    const FeltAction requested = positional_bots[engine.actor]->act(state);
    const auto [applied, violation] = validate_action(state, requested);
    engine.result.decisions.push_back(
        make_decision_record(state, requested, applied, violation));
    ++engine.decision_index;

    const std::size_t actor = engine.actor;
    const std::size_t opponent = 1U - actor;
    if (applied.type == FELT_ACTION_FOLD) {
      append_event(engine, engine.actor, FELT_EVENT_FOLD,
                   engine.street_contribution[actor]);
      finish_fold(engine, actor);
      return engine.result;
    }

    if (applied.type == FELT_ACTION_CHECK) {
      engine.acted_since_full_raise[actor] = true;
      append_event(engine, engine.actor, FELT_EVENT_CHECK,
                   engine.street_contribution[actor]);
    } else if (applied.type == FELT_ACTION_CALL) {
      const FeltChips highest = std::max(engine.street_contribution[0],
                                         engine.street_contribution[1]);
      const FeltChips amount =
          std::min(highest - engine.street_contribution[actor],
                   engine.stack[actor]);
      commit_chips(engine, actor, amount);
      engine.acted_since_full_raise[actor] = true;
      append_event(engine, engine.actor, FELT_EVENT_CALL,
                   engine.street_contribution[actor]);
    } else if (applied.type == FELT_ACTION_RAISE_TO) {
      const FeltChips previous_highest =
          std::max(engine.street_contribution[0],
                   engine.street_contribution[1]);
      const FeltChips amount =
          applied.amount_to - engine.street_contribution[actor];
      commit_chips(engine, actor, amount);
      const FeltChips raise_size = applied.amount_to - previous_highest;
      const bool full_raise = previous_highest == 0
                                  ? applied.amount_to >= config.big_blind
                                  : raise_size >= engine.last_full_raise_size;
      engine.acted_since_full_raise[actor] = true;
      if (full_raise) {
        engine.last_full_raise_size =
            previous_highest == 0 ? applied.amount_to : raise_size;
        engine.acted_since_full_raise[opponent] = false;
      }
      append_event(engine, engine.actor,
                   previous_highest == 0 ? FELT_EVENT_BET : FELT_EVENT_RAISE,
                   engine.street_contribution[actor]);
    } else {
      throw std::logic_error("validated action had an unknown type");
    }

    const bool matched_all_in =
        engine.street_contribution[0] == engine.street_contribution[1] &&
        (engine.stack[0] == 0 || engine.stack[1] == 0);
    if (matched_all_in) {
      finish_showdown(engine);
      return engine.result;
    }

    if (betting_round_complete(engine)) {
      if (engine.stack[0] == 0 || engine.stack[1] == 0 ||
          engine.street == FELT_STREET_RIVER) {
        finish_showdown(engine);
        return engine.result;
      }
      start_next_street(engine);
    } else {
      engine.actor = static_cast<std::uint32_t>(opponent);
    }
  }
}

}  // namespace felt

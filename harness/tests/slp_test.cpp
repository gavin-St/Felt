#include "felt/bot_api.h"
#include "felt/native_bot_runner.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr FeltCard card(std::uint8_t rank, std::uint8_t suit) {
  return static_cast<FeltCard>(rank * 4U + suit);
}

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

FeltGameState postflop_state(FeltCard first,
                             FeltCard second,
                             std::array<FeltCard, 5> board,
                             std::uint8_t board_count) {
  FeltGameState state{};
  state.abi_version = FELT_BOT_ABI_VERSION;
  state.struct_size = sizeof(FeltGameState);
  state.hole[0] = first;
  state.hole[1] = second;
  state.board_count = board_count;
  state.street = board_count == 3U ? FELT_STREET_FLOP
                                   : (board_count == 4U ? FELT_STREET_TURN
                                                       : FELT_STREET_RIVER);
  state.position = FELT_POSITION_BUTTON;
  state.legal_actions = FELT_LEGAL_CHECK | FELT_LEGAL_RAISE_TO;
  state.pot = 1000;
  state.my_stack = 19000;
  state.opp_stack = 19000;
  state.min_raise_to = 200;
  state.max_raise_to = 19000;
  state.decision_cap_us = 2000;
  for (std::size_t index = 0; index < board_count; ++index) {
    state.board[index] = board[index];
  }
  for (std::size_t index = board_count; index < 5U; ++index) {
    state.board[index] = FELT_INVALID_CARD;
  }
  return state;
}

/* We bet, the opponent raised: our own chips are already in this street. */
void face_reraise(FeltGameState& state,
                  FeltChips our_bet,
                  FeltChips their_raise,
                  FeltChips maximum) {
  state.pot += our_bet + their_raise;
  state.my_street_contribution = our_bet;
  state.opp_street_contribution = their_raise;
  state.to_call = their_raise - our_bet;
  state.opp_stack -= their_raise;
  state.legal_actions =
      FELT_LEGAL_FOLD | FELT_LEGAL_CALL | FELT_LEGAL_RAISE_TO;
  state.min_raise_to = their_raise * 2;
  state.max_raise_to = maximum;
}

void face_bet(FeltGameState& state, FeltChips amount, FeltChips maximum) {
  state.pot += amount;
  state.opp_street_contribution = amount;
  state.to_call = amount;
  state.opp_stack -= amount;
  state.legal_actions =
      FELT_LEGAL_FOLD | FELT_LEGAL_CALL | FELT_LEGAL_RAISE_TO;
  state.min_raise_to = amount * 2;
  state.max_raise_to = maximum;
}

void require_action(const FeltAction& action,
                    std::uint32_t type,
                    FeltChips amount,
                    const std::string& message) {
  require(action.type == type &&
              (type != FELT_ACTION_RAISE_TO || action.amount_to == amount),
          message);
}

void test_common_value_and_draw_policy(felt::NativeBotRunner& bot) {
  const std::string name(bot.name());
  const std::array<FeltActionEvent, 2> blinds{{
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP,
       FELT_EVENT_POST_SMALL_BLIND, 0U, 50},
      {FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP,
       FELT_EVENT_POST_BIG_BLIND, 0U, 100},
  }};
  FeltGameState preflop{};
  preflop.abi_version = FELT_BOT_ABI_VERSION;
  preflop.struct_size = sizeof(FeltGameState);
  preflop.hole[0] = card(12, 3);
  preflop.hole[1] = card(11, 3);
  preflop.street = FELT_STREET_PREFLOP;
  preflop.position = FELT_POSITION_BUTTON;
  preflop.legal_actions =
      FELT_LEGAL_FOLD | FELT_LEGAL_CALL | FELT_LEGAL_RAISE_TO;
  preflop.pot = 150;
  preflop.my_stack = 19950;
  preflop.opp_stack = 19900;
  preflop.my_street_contribution = 50;
  preflop.opp_street_contribution = 100;
  preflop.to_call = 50;
  preflop.min_raise_to = 200;
  preflop.max_raise_to = 20000;
  preflop.history = blinds.data();
  preflop.history_count = static_cast<std::uint32_t>(blinds.size());
  require_action(bot.act(preflop), FELT_ACTION_RAISE_TO, 250,
                 name + " did not use the shared preflop chart");

  FeltGameState top_pair = postflop_state(
      card(12, 3), card(11, 1),
      {card(12, 0), card(5, 2), card(0, 3), 0, 0}, 3U);
  top_pair.decision_random = 1;
  require_action(bot.act(top_pair), FELT_ACTION_RAISE_TO, 750,
                 name + " did not bet 75% pot with top pair");

  face_bet(top_pair, 400, 19000);
  if (name == "slp-balance") {
    require_action(bot.act(top_pair), FELT_ACTION_CALL, 0,
                   "balance version reraised one pair facing aggression");
  } else {
    require_action(bot.act(top_pair), FELT_ACTION_RAISE_TO, 1200,
                   name + " did not raise a bet to 3x");
    top_pair.max_raise_to = 1000;
    require_action(bot.act(top_pair), FELT_ACTION_RAISE_TO, 1000,
                   name + " did not use a short all-in");
    top_pair.legal_actions = FELT_LEGAL_FOLD | FELT_LEGAL_CALL;
    require_action(bot.act(top_pair), FELT_ACTION_CALL, 0,
                   name + " did not call when a value raise was unavailable");
  }

  FeltGameState small_pair = postflop_state(
      card(5, 3), card(12, 1),
      {card(11, 0), card(5, 2), card(0, 3), 0, 0}, 3U);
  face_bet(small_pair, 300, 19000);
  require_action(bot.act(small_pair), FELT_ACTION_CALL, 0,
                 name + " did not call with a smaller pair");

  FeltGameState flush_draw = postflop_state(
      card(12, 3), card(11, 3),
      {card(10, 3), card(5, 3), card(0, 0), 0, 0}, 3U);
  face_bet(flush_draw, 300, 19000);
  require_action(bot.act(flush_draw), FELT_ACTION_CALL, 0,
                 name + " did not call with a live draw");
}

void test_balance_street_local_policy(felt::NativeBotRunner& balance) {
  FeltGameState trapped_top_pair = postflop_state(
      card(12, 3), card(11, 1),
      {card(12, 0), card(5, 2), card(0, 3), 0, 0}, 3U);
  trapped_top_pair.decision_random = 0;
  require_action(balance.act(trapped_top_pair), FELT_ACTION_CHECK, 0,
                 "balance version did not check its top-pair trap branch");
  face_bet(trapped_top_pair, 400, 19000);
  trapped_top_pair.decision_random = 1;
  require_action(balance.act(trapped_top_pair), FELT_ACTION_CALL, 0,
                 "balance version reraised one pair after a new random roll");

  FeltGameState two_pair = postflop_state(
      card(12, 3), card(11, 1),
      {card(12, 0), card(11, 2), card(0, 3), 0, 0}, 3U);
  face_bet(two_pair, 400, 19000);
  two_pair.decision_random = 1;
  require_action(balance.act(two_pair), FELT_ACTION_CALL, 0,
                 "balance version did not call with two pair");
  two_pair.decision_random = 0;
  require_action(balance.act(two_pair), FELT_ACTION_CALL, 0,
                 "balance version did not trap with two pair");

  FeltGameState set = postflop_state(
      card(12, 3), card(12, 1),
      {card(12, 0), card(5, 2), card(0, 3), 0, 0}, 3U);
  face_bet(set, 400, 19000);
  set.decision_random = 1;
  require_action(balance.act(set), FELT_ACTION_RAISE_TO, 1200,
                 "balance version did not reraise a set on its aggressive branch");
  set.decision_random = 0;
  require_action(balance.act(set), FELT_ACTION_CALL, 0,
                 "balance version did not call a set on its trap branch");

  FeltGameState small_pair = postflop_state(
      card(5, 3), card(12, 1),
      {card(11, 0), card(5, 2), card(0, 3), 0, 0}, 3U);
  face_bet(small_pair, 1200, 19000);
  require_action(balance.act(small_pair), FELT_ACTION_CALL, 0,
                 "balance version folded a smaller pair to an overbet");

  FeltGameState flush_draw = postflop_state(
      card(12, 3), card(11, 3),
      {card(10, 3), card(5, 3), card(0, 0), 0, 0}, 3U);
  face_bet(flush_draw, 1200, 19000);
  require_action(balance.act(flush_draw), FELT_ACTION_CALL, 0,
                 "balance version folded a draw to an overbet");

  /* Facing a raise of our own bet, one pair calls without reraising and only a
   * third of the draws continue. */
  FeltGameState raised_top_pair = postflop_state(
      card(12, 3), card(11, 1),
      {card(12, 0), card(5, 2), card(0, 3), 0, 0}, 3U);
  face_reraise(raised_top_pair, 300, 1200, 19000);
  raised_top_pair.decision_random = 1;
  require_action(balance.act(raised_top_pair), FELT_ACTION_CALL, 0,
                 "balance version folded one pair to a raise");

  FeltGameState raised_two_pair = postflop_state(
      card(12, 3), card(11, 1),
      {card(12, 0), card(11, 2), card(0, 3), 0, 0}, 3U);
  face_reraise(raised_two_pair, 300, 1200, 19000);
  raised_two_pair.decision_random = 0;
  require_action(balance.act(raised_two_pair), FELT_ACTION_CALL, 0,
                 "balance version gave up two pair to a raise");
  raised_two_pair.decision_random = 1;
  require_action(balance.act(raised_two_pair), FELT_ACTION_CALL, 0,
                 "balance version reraised two pair after facing a raise");

  FeltGameState raised_draw = postflop_state(
      card(12, 3), card(11, 3),
      {card(10, 3), card(5, 3), card(0, 0), 0, 0}, 3U);
  face_reraise(raised_draw, 300, 1200, 19000);
  raised_draw.decision_random = 0;
  require_action(balance.act(raised_draw), FELT_ACTION_CALL, 0,
                 "balance version folded its continuing draw branch to a raise");
  raised_draw.decision_random = 1ULL << 16U;
  require_action(balance.act(raised_draw), FELT_ACTION_FOLD, 0,
                 "balance version continued every draw against a raise");
}

void test_shared_bluff_guard(felt::NativeBotRunner& bot) {
  for (const std::uint8_t count : {3U, 4U, 5U}) {
    auto state = postflop_state(card(6,0), card(4,1),
        {card(12,2), card(11,3), card(9,0), card(2,1), card(1,2)}, count);
    state.decision_random = 1;
    state.my_stack = state.opp_stack = 499;
    state.max_raise_to = 499;
    require_action(bot.act(state), FELT_ACTION_CHECK, 0, "SLP bluffed below half SPR");
  }
  auto state = postflop_state(card(6,0), card(4,1),
      {card(12,2), card(11,3), card(9,0), card(2,1), card(1,2)}, 5U);
  face_bet(state, 19000, 0);
  state.legal_actions = FELT_LEGAL_FOLD | FELT_LEGAL_CALL;
  state.decision_random = 1;
  require_action(bot.act(state), FELT_ACTION_FOLD, 0, "SLP impossible bluff became a call");
}

void test_shared_board_policy(felt::NativeBotRunner& bot) {
  // Four-flush tiers on both the turn and river; the offsuit ace must
  // never upgrade a low heart. Include all three strong and weak ranks.
  for (const std::uint8_t count : {4U, 5U}) {
    for (const std::uint8_t rank : {0U, 1U, 2U, 10U, 11U, 12U}) {
      auto state = postflop_state(card(rank, 3), card(rank == 12 ? 11 : 12, 1),
          {card(9, 3), card(7, 3), card(5, 3), card(3, 3), card(0, 0)}, count);
      state.decision_random = 1;
      require_action(bot.act(state), rank >= 10 ? FELT_ACTION_RAISE_TO
                                                : FELT_ACTION_CHECK, 750,
                     "SLP four-flush strength did not follow its suited card");
      face_bet(state, 300, 19000);
      require_action(bot.act(state), rank >= 10 ? FELT_ACTION_RAISE_TO
                                                : FELT_ACTION_CALL, 900,
                     "SLP raised a weak four-flush or failed to raise a strong one");
    }
  }
  struct Case {
    FeltCard first;
    FeltCard second;
    std::array<FeltCard, 5> board;
    bool value;
  };
  const Case cases[] = {
      {card(0,0), card(0,1), {card(7,0), card(7,1), card(7,3), card(11,2), card(1,0)}, false},
      {card(12,0), card(12,1), {card(7,0), card(7,1), card(7,3), card(11,2), card(1,0)}, true},
      {card(0,0), card(1,1), {card(12,0), card(12,1), card(0,3), card(0,2), card(2,0)}, false},
      {card(10, 0), card(0, 1), {card(5, 0), card(5, 1), card(5, 3), card(11, 2), card(1, 0)}, false},
      {card(11, 0), card(0, 1), {card(12, 0), card(12, 1), card(12, 3), card(12, 2), card(1, 0)}, false},
      {card(12, 0), card(12, 1), {card(5, 0), card(5, 1), card(5, 3), card(11, 2), card(11, 0)}, true},
      {card(12, 0), card(0, 1), {card(5, 0), card(5, 1), card(5, 3), card(11, 2), card(11, 0)}, false},
      {card(8, 0), card(0, 1), {card(3, 0), card(4, 1), card(5, 3), card(6, 2), card(7, 0)}, true},
      {card(12, 0), card(0, 1), {card(3, 0), card(4, 1), card(5, 3), card(6, 2), card(7, 0)}, false},
      {card(0, 0), card(1, 1), {card(8, 3), card(9, 3), card(10, 3), card(11, 3), card(12, 3)}, false},
  };
  for (const auto& example : cases) {
    auto state = postflop_state(example.first, example.second, example.board, 5U);
    state.decision_random = 1;
    require_action(bot.act(state), example.value ? FELT_ACTION_RAISE_TO : FELT_ACTION_CHECK,
                   750, "SLP confused shared board value with a private improvement");
    face_bet(state, 300, 19000);
    require_action(bot.act(state), example.value ? FELT_ACTION_RAISE_TO : FELT_ACTION_CALL,
                   900, "SLP lost showdown value on a shared board");
  }
}

void test_two_pair_policy(felt::NativeBotRunner& fold,
                          felt::NativeBotRunner& bluff,
                          felt::NativeBotRunner& balance,
                          felt::NativeBotRunner& exploit_fold,
                          felt::NativeBotRunner& exploit_solved) {
  FeltGameState board_only = postflop_state(
      card(12, 3), card(10, 1),
      {card(11, 0), card(11, 2), card(5, 3), card(5, 0), card(0, 1)}, 5U);
  board_only.decision_random = 1;
  for (auto* bot : {&fold, &bluff, &balance, &exploit_fold, &exploit_solved}) {
    require_action(bot->act(board_only), FELT_ACTION_CHECK, 0,
                   "ace kicker on board two pair was treated as air");
  }

  FeltGameState under = postflop_state(
      card(12, 3), card(0, 1),
      {card(11, 0), card(11, 2), card(10, 3), card(6, 0), card(0, 2)}, 5U);
  under.decision_random = 1;
  require_action(fold.act(under), FELT_ACTION_CHECK, 0,
                 "fold version did not treat under two pair as showdown value");
  require_action(bluff.act(under), FELT_ACTION_CHECK, 0,
                 "bluff version bluffed under two pair");
  require_action(balance.act(under), FELT_ACTION_CHECK, 0,
                 "balance version did not check under two pair");
  require_action(exploit_fold.act(under), FELT_ACTION_CHECK, 0,
                 "fold exploit bluffed under two pair");
  require_action(exploit_solved.act(under), FELT_ACTION_CHECK, 0,
                 "solved exploit bluffed under two pair");

  FeltGameState middle = postflop_state(
      card(12, 3), card(6, 1),
      {card(11, 0), card(11, 2), card(10, 3), card(6, 0), card(0, 2)}, 5U);
  middle.decision_random = 1;
  require_action(fold.act(middle), FELT_ACTION_CHECK, 0,
                 "fold version did not treat middle two pair as showdown value");
  require_action(bluff.act(middle), FELT_ACTION_CHECK, 0,
                 "bluff version bluffed middle two pair");
  require_action(balance.act(middle), FELT_ACTION_CHECK, 0,
                 "balance version did not check middle two pair");

  FeltGameState over = postflop_state(
      card(12, 3), card(5, 1),
      {card(11, 0), card(11, 2), card(5, 3), card(1, 0), card(0, 2)}, 5U);
  over.decision_random = 1;
  require_action(fold.act(over), FELT_ACTION_RAISE_TO, 750,
                 "fold version did not value-bet over two pair");
  require_action(bluff.act(over), FELT_ACTION_RAISE_TO, 750,
                 "bluff version did not value-bet over two pair");
  require_action(balance.act(over), FELT_ACTION_RAISE_TO, 750,
                 "balance version did not value-bet over two pair");

  FeltGameState both_holes = postflop_state(
      card(12, 3), card(11, 1),
      {card(12, 0), card(11, 2), card(5, 3), card(1, 0), card(0, 2)}, 5U);
  both_holes.decision_random = 1;
  require_action(fold.act(both_holes), FELT_ACTION_RAISE_TO, 750,
                 "fold version did not value-bet both-hole-card two pair");
  require_action(bluff.act(both_holes), FELT_ACTION_RAISE_TO, 750,
                 "bluff version did not value-bet both-hole-card two pair");
  require_action(balance.act(both_holes), FELT_ACTION_RAISE_TO, 750,
                 "balance version did not value-bet both-hole-card two pair");

  face_bet(under, 300, 19000);
  require_action(fold.act(under), FELT_ACTION_CALL, 0,
                 "fold version did not call with under two pair");
  require_action(bluff.act(under), FELT_ACTION_CALL, 0,
                 "bluff version did not call with under two pair");
  require_action(balance.act(under), FELT_ACTION_CALL, 0,
                 "balance version did not call with under two pair");
  require_action(exploit_fold.act(under), FELT_ACTION_FOLD, 0,
                 "fold exploit continued with under two pair");
  require_action(exploit_solved.act(under), FELT_ACTION_FOLD, 0,
                 "solved exploit continued with under two pair");

  face_bet(middle, 300, 19000);
  require_action(fold.act(middle), FELT_ACTION_CALL, 0,
                 "fold version did not call with middle two pair");
  require_action(bluff.act(middle), FELT_ACTION_CALL, 0,
                 "bluff version did not call with middle two pair");
  require_action(balance.act(middle), FELT_ACTION_CALL, 0,
                 "balance version did not call with middle two pair");
  require_action(exploit_fold.act(middle), FELT_ACTION_FOLD, 0,
                 "fold exploit continued with middle two pair");

  FeltGameState raised_under = postflop_state(
      card(12, 3), card(0, 1),
      {card(11, 0), card(11, 2), card(10, 3), card(6, 0), card(0, 2)}, 5U);
  face_reraise(raised_under, 300, 1200, 19000);
  require_action(balance.act(raised_under), FELT_ACTION_FOLD, 0,
                 "balance version treated under two pair as a strong two pair");

  face_bet(over, 300, 19000);
  require_action(fold.act(over), FELT_ACTION_RAISE_TO, 900,
                 "fold version did not raise over two pair");
  require_action(balance.act(over), FELT_ACTION_CALL, 0,
                 "balance version did not call with over two pair");
  require_action(exploit_fold.act(over), FELT_ACTION_RAISE_TO, 900,
                 "fold exploit folded over two pair");
  require_action(exploit_solved.act(over), FELT_ACTION_RAISE_TO, 900,
                 "solved exploit folded over two pair");

  face_bet(both_holes, 300, 19000);
  require_action(balance.act(both_holes), FELT_ACTION_CALL, 0,
                 "balance version did not call with both-hole-card two pair");
  require_action(exploit_fold.act(both_holes), FELT_ACTION_RAISE_TO, 900,
                 "fold exploit folded both-hole-card two pair");

  FeltGameState raised_over = postflop_state(
      card(12, 3), card(5, 1),
      {card(11, 0), card(11, 2), card(5, 3), card(1, 0), card(0, 2)}, 5U);
  face_reraise(raised_over, 300, 1200, 19000);
  require_action(balance.act(raised_over), FELT_ACTION_CALL, 0,
                 "balance version did not trap with over two pair");

  FeltGameState raised_both = postflop_state(
      card(12, 3), card(11, 1),
      {card(12, 0), card(11, 2), card(5, 3), card(1, 0), card(0, 2)}, 5U);
  face_reraise(raised_both, 300, 1200, 19000);
  require_action(balance.act(raised_both), FELT_ACTION_CALL, 0,
                 "balance version did not trap with both-hole-card two pair");
  raised_both.decision_random = 1;
  require_action(balance.act(raised_both), FELT_ACTION_CALL, 0,
                 "balance version reraised both-hole-card two pair");
}

FeltGameState air_flop() {
  return postflop_state(
      card(6, 0), card(1, 1),
      {card(11, 2), card(10, 0), card(0, 3), 0, 0}, 3U);
}

FeltGameState missed_draw_river() {
  return postflop_state(
      card(12, 3), card(11, 3),
      {card(10, 3), card(5, 3), card(0, 0), card(1, 1), card(2, 2)},
      5U);
}

FeltGameState solved_exploit_preflop(FeltCard first,
                                     FeltCard second,
                                     std::uint32_t position,
                                     bool facing_all_in) {
  static constexpr std::array<FeltActionEvent, 2> blinds{{
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP,
       FELT_EVENT_POST_SMALL_BLIND, 0U, 50},
      {FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP,
       FELT_EVENT_POST_BIG_BLIND, 0U, 100},
  }};
  FeltGameState state{};
  state.abi_version = FELT_BOT_ABI_VERSION;
  state.struct_size = sizeof(FeltGameState);
  state.hole[0] = first;
  state.hole[1] = second;
  state.street = FELT_STREET_PREFLOP;
  state.position = position;
  state.my_stack = 19950;
  state.opp_stack = facing_all_in ? 0 : 19900;
  state.my_street_contribution = position == FELT_POSITION_BUTTON ? 50 : 100;
  state.opp_street_contribution = facing_all_in ? 20000 : 100;
  state.pot = facing_all_in ? 20050 : 150;
  state.to_call = facing_all_in ? 19950 : 50;
  state.min_raise_to = 200;
  state.max_raise_to = 20000;
  state.legal_actions = facing_all_in
                            ? FELT_LEGAL_FOLD | FELT_LEGAL_CALL
                            : FELT_LEGAL_FOLD | FELT_LEGAL_CALL |
                                  FELT_LEGAL_RAISE_TO;
  state.history = blinds.data();
  state.history_count = static_cast<std::uint32_t>(blinds.size());
  return state;
}

void test_air_policies(felt::NativeBotRunner& fold,
                       felt::NativeBotRunner& bluff,
                       felt::NativeBotRunner& balance) {
  FeltGameState checked_air = air_flop();
  checked_air.decision_random = 0;
  require_action(balance.act(checked_air), FELT_ACTION_CHECK, 0,
                 "balance version did not check its passive air half");
  checked_air.decision_random = 1;
  require_action(balance.act(checked_air), FELT_ACTION_RAISE_TO, 750,
                 "balance version did not bluff half its air when checked to");

  FeltGameState air = air_flop();
  face_bet(air, 300, 19000);
  require_action(fold.act(air), FELT_ACTION_FOLD, 0,
                 "fold version did not fold air");
  require_action(bluff.act(air), FELT_ACTION_RAISE_TO, 900,
                 "bluff version did not raise air");
  air.decision_random = 0;
  require_action(balance.act(air), FELT_ACTION_FOLD, 0,
                 "balance version's passive half changed");
  air.decision_random = 1;
  require_action(balance.act(air), FELT_ACTION_FOLD, 0,
                 "balance version bluff-raised air facing aggression");

  FeltGameState river = missed_draw_river();
  face_bet(river, 300, 19000);
  require_action(fold.act(river), FELT_ACTION_FOLD, 0,
                 "fold version did not treat a missed river draw as air");
  require_action(bluff.act(river), FELT_ACTION_RAISE_TO, 900,
                 "bluff version did not bluff a missed river draw");
  river.decision_random = 1;
  require_action(balance.act(river), FELT_ACTION_FOLD, 0,
                 "balance version bluff-raised a missed river draw");
}

void test_exploit_fold(felt::NativeBotRunner& exploit) {
  FeltGameState air = air_flop();
  require_action(exploit.act(air), FELT_ACTION_RAISE_TO, 750,
                 "fold exploit did not bluff air when checked to");
  face_bet(air, 300, 19000);
  require_action(exploit.act(air), FELT_ACTION_FOLD, 0,
                 "fold exploit continued with air versus aggression");

  FeltGameState top_pair = postflop_state(
      card(12, 3), card(11, 1),
      {card(12, 0), card(5, 2), card(0, 3), 0, 0}, 3U);
  face_bet(top_pair, 300, 19000);
  require_action(exploit.act(top_pair), FELT_ACTION_FOLD, 0,
                 "fold exploit continued with only top pair");

  FeltGameState overpair = postflop_state(
      card(12, 3), card(12, 1),
      {card(11, 0), card(5, 2), card(0, 3), 0, 0}, 3U);
  face_bet(overpair, 300, 19000);
  require_action(exploit.act(overpair), FELT_ACTION_RAISE_TO, 900,
                 "fold exploit did not continue with an overpair");
}

void test_exploit_solved(felt::NativeBotRunner& exploit) {
  FeltGameState seven_deuce = solved_exploit_preflop(
      card(5, 0), card(0, 1), FELT_POSITION_BUTTON, false);
  require_action(exploit.act(seven_deuce), FELT_ACTION_RAISE_TO, 200,
                 "solved exploit did not min-raise a weak opening hand");

  static constexpr std::array<FeltActionEvent, 4> reraise_history{{
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP,
       FELT_EVENT_POST_SMALL_BLIND, 0U, 50},
      {FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP,
       FELT_EVENT_POST_BIG_BLIND, 0U, 100},
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 0U, 200},
      {FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 0U,
       1000},
  }};
  FeltGameState facing_reraise = seven_deuce;
  facing_reraise.pot = 1200;
  facing_reraise.my_stack = 19800;
  facing_reraise.opp_stack = 19000;
  facing_reraise.my_street_contribution = 200;
  facing_reraise.opp_street_contribution = 1000;
  facing_reraise.to_call = 800;
  facing_reraise.min_raise_to = 1800;
  facing_reraise.history = reraise_history.data();
  facing_reraise.history_count =
      static_cast<std::uint32_t>(reraise_history.size());
  require_action(exploit.act(facing_reraise), FELT_ACTION_FOLD, 0,
                 "solved exploit min-reraised a weak hand after opening");

  FeltGameState aces = solved_exploit_preflop(
      card(12, 0), card(12, 1), FELT_POSITION_BUTTON, true);
  require_action(exploit.act(aces), FELT_ACTION_CALL, 0,
                 "solved exploit folded aces after its min-raise was shoved on");

  FeltGameState kings = solved_exploit_preflop(
      card(11, 0), card(11, 1), FELT_POSITION_BUTTON, true);
  require_action(exploit.act(kings), FELT_ACTION_FOLD, 0,
                 "solved exploit called kings into the target's aces/kings range");

  FeltGameState queens = solved_exploit_preflop(
      card(10, 0), card(10, 1), FELT_POSITION_BIG_BLIND, true);
  require_action(exploit.act(queens), FELT_ACTION_CALL, 0,
                 "solved exploit did not use the solved BB calling range");

  FeltGameState ace_king_offsuit = solved_exploit_preflop(
      card(12, 0), card(11, 1), FELT_POSITION_BIG_BLIND, true);
  require_action(exploit.act(ace_king_offsuit), FELT_ACTION_FOLD, 0,
                 "solved exploit widened the solved BB calling range");

  test_exploit_fold(exploit);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6) {
    std::cerr <<
        "expected fold, bluff, balance, fold-exploit, and solved-exploit bot "
        "library paths\n";
    return 2;
  }
  try {
    felt::NativeBotRunner fold(argv[1]);
    felt::NativeBotRunner bluff(argv[2]);
    felt::NativeBotRunner balance(argv[3]);
    felt::NativeBotRunner exploit_fold(argv[4]);
    felt::NativeBotRunner exploit_solved(argv[5]);
    for (auto* bot : {&fold, &bluff, &balance, &exploit_fold, &exploit_solved}) {
      test_shared_bluff_guard(*bot);
    }
    test_common_value_and_draw_policy(fold);
    test_common_value_and_draw_policy(bluff);
    test_common_value_and_draw_policy(balance);
    test_shared_board_policy(fold);
    test_shared_board_policy(bluff);
    test_shared_board_policy(balance);
    test_balance_street_local_policy(balance);
    test_two_pair_policy(fold, bluff, balance, exploit_fold, exploit_solved);
    test_air_policies(fold, bluff, balance);
    test_exploit_fold(exploit_fold);
    test_exploit_solved(exploit_solved);
  } catch (const std::exception& error) {
    std::cerr << "slp_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

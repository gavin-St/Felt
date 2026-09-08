#include "felt/bot_api.h"
#include "felt/bot_kit.h"
#include "felt/native_bot_runner.hpp"

#include "board_value.h"
#include "bet_sizing.h"
#include "call_rules.h"
#include "range_read.h"
#include "raise_rules.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr FeltCard card(std::uint8_t rank, std::uint8_t suit) {
  return static_cast<FeltCard>(rank * 4U + suit);
}

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

constexpr std::uint32_t kAll =
    FELT_LEGAL_FOLD | FELT_LEGAL_CALL | FELT_LEGAL_RAISE_TO;
constexpr std::uint32_t kNoBet = FELT_LEGAL_CHECK | FELT_LEGAL_RAISE_TO;

struct Hand {
  std::vector<FeltActionEvent> history;

  void add(std::uint32_t position, std::uint32_t street, std::uint32_t type,
           FeltChips amount_to) {
    FeltActionEvent event{};
    event.position = position;
    event.street = street;
    event.type = type;
    event.amount_to = amount_to;
    history.push_back(event);
  }

  void blinds() {
    add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP,
        FELT_EVENT_POST_SMALL_BLIND, 50);
    add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP,
        FELT_EVENT_POST_BIG_BLIND, 100);
  }

  /* `mine` is our own total contribution on this street: non-zero means the
   * bet in front of us is a raise of ours, which several rules turn on. */
  FeltGameState state(std::uint32_t street, FeltChips pot, FeltChips to_call,
                      std::uint32_t legal, FeltChips mine = 0) const {
    FeltGameState s{};
    s.abi_version = FELT_BOT_ABI_VERSION;
    s.struct_size = sizeof(FeltGameState);
    s.street = street;
    s.position = FELT_POSITION_BIG_BLIND;
    s.pot = pot;
    s.to_call = to_call;
    s.my_stack = 20000 - pot / 2;
    s.opp_stack = 20000 - pot / 2;
    s.my_street_contribution = mine;
    s.opp_street_contribution = to_call + mine;
    s.min_raise_to = to_call * 2 > 100 ? to_call * 2 : 100;
    s.max_raise_to = 20000;
    s.legal_actions = legal;
    s.decision_random = UINT64_C(0x01000000);
    std::memset(s.board, FELT_INVALID_CARD, sizeof(s.board));
    s.history = history.data();
    s.history_count = static_cast<std::uint32_t>(history.size());
    return s;
  }
};

void set_board(FeltGameState& state, const std::vector<FeltCard>& board) {
  std::memset(state.board, FELT_INVALID_CARD, sizeof(state.board));
  for (std::size_t index = 0; index < board.size(); index++) {
    state.board[index] = board[index];
  }
  state.board_count = static_cast<std::uint8_t>(board.size());
}

FeltHandValue value_of(FeltGameState& state,
                       const std::array<FeltCard, 2>& hole,
                       const std::vector<FeltCard>& board) {
  state.hole[0] = hole[0];
  state.hole[1] = hole[1];
  set_board(state, board);
  const FeltMadeHand made =
      felt_made_hand(state.hole, state.board, state.board_count);
  const FeltDraws draws =
      felt_draws(state.hole, state.board, state.board_count);
  const FeltBoardTexture texture =
      felt_board_texture(state.board, state.board_count);
  return felt_board_relative_value(&state, &made, &draws, &texture);
}

/* A line that has shown nothing scores low; one that has raised twice does
 * not. This is the whole opponent model. */
void test_range_score() {
  const std::uint32_t them = FELT_POSITION_BUTTON;

  Hand checked;
  checked.blinds();
  checked.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  checked.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  checked.add(them, FELT_STREET_FLOP, FELT_EVENT_CHECK, 0);
  const std::vector<FeltCard> dry = {card(11, 2), card(5, 1), card(2, 3)};
  const FeltBoardTexture texture =
      felt_board_texture(dry.data(), static_cast<std::uint8_t>(dry.size()));
  const FeltGameState quiet_state = checked.state(FELT_STREET_FLOP, 500, 0, kNoBet);
  const FeltRangeRead quiet = felt_read_range(&quiet_state, &texture);

  Hand bet = checked;
  bet.history.pop_back();
  bet.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 330);
  const FeltGameState bet_state = bet.state(FELT_STREET_FLOP, 830, 330, kAll);
  const FeltRangeRead betting = felt_read_range(&bet_state, &texture);

  Hand raised = checked;
  raised.history.pop_back();
  raised.add(FELT_POSITION_BIG_BLIND, FELT_STREET_FLOP, FELT_EVENT_BET, 330);
  raised.add(them, FELT_STREET_FLOP, FELT_EVENT_RAISE, 1200);
  FeltGameState raised_state =
      raised.state(FELT_STREET_FLOP, 1860, 870, kAll);
  raised_state.my_street_contribution = 330;
  const FeltRangeRead raising = felt_read_range(&raised_state, &texture);

  require(quiet.valid && betting.valid && raising.valid, "range read failed");
  require(quiet.score < betting.score,
          "a checking range did not score below a betting one");
  require(betting.score < raising.score,
          "a betting range did not score below a raising one");
  require(quiet.score < 35 && raising.score > 60,
          "the range scale is not spread out: " + std::to_string(quiet.score) +
              " to " + std::to_string(raising.score));
}

void test_adjusted_score_and_bluff_estimate() {
  FeltHandValue value{};
  value.valid = true;
  value.points = 70;
  FeltRangeRead read{};
  read.valid = true;
  read.score = 80;
  require(felt_adjusted_hand_score(&value, &read) == 55.0,
          "adjusted score did not subtract half the range distance from 50");
  require(felt_range_delta(&value, &read) == 5.0,
          "delta was not adjusted score minus 50");

  const std::uint32_t them = FELT_POSITION_BUTTON;
  Hand hand;
  hand.blinds();
  hand.add(them, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 100);
  hand.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CHECK, 100);
  hand.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 200);
  const std::vector<FeltCard> board = {card(9, 2), card(8, 1), card(7, 2)};
  const FeltBoardTexture texture = felt_board_texture(board.data(), 3U);
  const FeltGameState estimate_state =
      hand.state(FELT_STREET_FLOP, 400, 200, kAll);
  const FeltRangeRead estimated = felt_read_range(&estimate_state, &texture);
  const int expected_air = estimated.polarisation * (100 - estimated.score);
  int expected_bluff = 500 + 6000 * expected_air / 10000;
  if (expected_bluff < 500) expected_bluff = 500;
  if (expected_bluff > 4500) expected_bluff = 4500;
  require(estimated.air_share_basis_points == expected_air &&
              estimated.bluff_rate_basis_points == expected_bluff,
          "air-share bluff estimate did not use P * (100 - R)");
}

/* Whether our previous bet was large is measured against the pot before that
 * bet, not against a pot that already contains it. A pot-sized lead therefore
 * avoids the extra raise penalty while a three-quarter-pot lead does not. */
void test_large_bet_raise_penalty() {
  FeltRangeRead read{};
  read.valid = true;
  read.polarisation = 20;
  read.bluff_rate_basis_points = 2000;

  FeltGameState pot_bet{};
  pot_bet.street = FELT_STREET_TURN;
  pot_bet.pot = 5000;
  pot_bet.my_street_contribution = 1000;
  pot_bet.opp_street_contribution = 3000;
  pot_bet.to_call = 2000;
  require(felt_bluff_catch_frequency(&pot_bet, 0.0, &read) == 56,
          "a pot-sized bet was mistaken for a small bet after a raise");

  require(felt_bluff_catch_frequency(&pot_bet, -15.0, &read) == 28,
          "a thin pot-sized bluff-catcher did not defend below the near band");

  FeltGameState three_quarters = pot_bet;
  three_quarters.pot = 4000;
  three_quarters.my_street_contribution = 750;
  three_quarters.opp_street_contribution = 2250;
  three_quarters.to_call = 1500;
  require(felt_bluff_catch_frequency(&three_quarters, 0.0, &read) == 52,
          "a three-quarter-pot bet was mistaken for a large bet");
}

void test_street_and_position_bluff_catch() {
  FeltGameState state{};
  state.position = FELT_POSITION_BUTTON;
  state.pot = 200;
  state.to_call = 100;
  state.opp_street_contribution = 100;

  FeltRangeRead read{};
  read.valid = true;
  read.bluff_rate_basis_points = 2000;

  state.street = FELT_STREET_FLOP;
  require(felt_bluff_catch_frequency(&state, 0.0, &read) == 42,
          "flop bluff-catch did not realize sixty percent of its baseline");
  state.street = FELT_STREET_TURN;
  require(felt_bluff_catch_frequency(&state, 0.0, &read) == 56,
          "turn bluff-catch did not realize eighty percent of its baseline");
  state.street = FELT_STREET_RIVER;
  require(felt_bluff_catch_frequency(&state, 0.0, &read) == 70,
          "river bluff-catch did not retain its full baseline");

  require(felt_bluff_catch_frequency(&state, -27.0, &read) == 35,
          "in-position first-bet thin band did not extend to minus thirty");
  state.position = FELT_POSITION_BIG_BLIND;
  require(felt_bluff_catch_frequency(&state, -27.0, &read) == 35,
          "river retained an out-of-position realization penalty");
  state.street = FELT_STREET_FLOP;
  require(felt_bluff_catch_frequency(&state, -27.0, &read) == 0,
          "early-street out-of-position thin band extended below minus twenty-five");
}

/* The range reader records who took each preflop action, rather than treating
 * two raises as one generic three-bet pot. */
void test_preflop_actor_model() {
  const std::uint32_t them = FELT_POSITION_BUTTON;
  const std::uint32_t us = FELT_POSITION_BIG_BLIND;
  const std::vector<FeltCard> board = {card(11, 2), card(7, 1), card(2, 3)};
  const FeltBoardTexture texture = felt_board_texture(board.data(), 3U);

  Hand they_three_bet;
  they_three_bet.blinds();
  they_three_bet.add(us, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  they_three_bet.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 900);
  they_three_bet.add(us, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 900);
  const FeltGameState aggressive =
      they_three_bet.state(FELT_STREET_FLOP, 1800, 0, kNoBet);
  const FeltRangeRead aggressive_read = felt_read_range(&aggressive, &texture);

  Hand they_call_three_bet;
  they_call_three_bet.blinds();
  they_call_three_bet.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  they_call_three_bet.add(us, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 900);
  they_call_three_bet.add(them, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 900);
  const FeltGameState passive =
      they_call_three_bet.state(FELT_STREET_FLOP, 1800, 0, kNoBet);
  const FeltRangeRead passive_read = felt_read_range(&passive, &texture);

  require(aggressive_read.opponent_preflop_line == FELT_PREFLOP_LINE_THREE_BET,
          "opponent three-bet was not attributed to the opponent");
  require(passive_read.opponent_preflop_line ==
              FELT_PREFLOP_LINE_CALL_THREE_BET,
          "opponent call of our three-bet was not recorded");
  require(aggressive_read.score > passive_read.score,
          "opponent three-bet did not claim more than calling our three-bet");
}

void test_position_aware_unopened_range() {
  const std::vector<FeltCard> board = {card(11, 2), card(7, 1), card(2, 3)};
  const FeltBoardTexture texture = felt_board_texture(board.data(), 3U);

  Hand out_of_position;
  out_of_position.blinds();
  out_of_position.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP,
                      FELT_EVENT_RAISE, 250);
  out_of_position.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP,
                      FELT_EVENT_CALL, 250);
  const FeltGameState first =
      out_of_position.state(FELT_STREET_FLOP, 500, 0, kNoBet);
  const FeltRangeRead first_read = felt_read_range(&first, &texture);

  Hand in_position = out_of_position;
  in_position.add(FELT_POSITION_BIG_BLIND, FELT_STREET_FLOP,
                  FELT_EVENT_CHECK, 0);
  FeltGameState checked = in_position.state(FELT_STREET_FLOP, 500, 0, kNoBet);
  checked.position = FELT_POSITION_BUTTON;
  const FeltRangeRead checked_read = felt_read_range(&checked, &texture);

  require(first_read.hero_out_of_position &&
              !first_read.opponent_acted_this_street,
          "first-to-act position was not recognized");
  require(!checked_read.hero_out_of_position &&
              checked_read.opponent_checked_this_street,
          "in-position check-back opportunity was not recognized");
  require(first_read.score > checked_read.score,
          "an untouched in-position range was treated like a range that checked");
}

void test_cheap_call_rules() {
  FeltHandValue air{};
  air.valid = true;
  FeltHandValue pair = air;
  pair.player_made_pair_or_better = true;

  FeltGameState state{};
  state.street = FELT_STREET_TURN;
  state.opp_stack = 1000;
  state.pot = 1099;
  state.to_call = 99;
  require(felt_forced_cheap_call(&state, &air),
          "folded air for less than ten percent of the prior pot");

  state.pot = 1100;
  state.to_call = 100;
  require(!felt_forced_cheap_call(&state, &air),
          "the strict ten-percent boundary included exactly ten percent");

  state.street = FELT_STREET_RIVER;
  state.pot = 1199;
  state.to_call = 199;
  require(felt_forced_cheap_call(&state, &pair),
          "folded a private pair for less than twenty percent on the river");
  require(!felt_forced_cheap_call(&state, &air),
          "called the river twenty-percent rail without a private pair");

  state.street = FELT_STREET_TURN;
  require(!felt_forced_cheap_call(&state, &pair),
          "used the pair rail before the river against chips behind");
  state.opp_stack = 0;
  require(felt_forced_cheap_call(&state, &pair),
          "folded the pair rail against an all-in wager");
}

void test_balanced_bluff_frequency() {
  FeltGameState state{};
  state.street = FELT_STREET_FLOP;
  state.position = FELT_POSITION_BUTTON;
  state.pot = 300;
  require(felt_balanced_bluff_frequency(&state, 100, false) == 10,
          "one-third-pot bluff did not realize half its balanced share");
  require(felt_balanced_bluff_frequency(&state, 375, false) == 18,
          "one-and-a-quarter-pot bluff did not realize half its share");
  require(felt_balanced_bluff_frequency(&state, 375, true) == 36,
          "in-position opening air did not realize its full base share");
  state.position = FELT_POSITION_BIG_BLIND;
  require(felt_balanced_bluff_frequency(&state, 375, true) == 9,
          "out-of-position pure air was not reduced");
  require(felt_balanced_bluff_frequency(&state, 375, false) == 18,
          "out-of-position semi-bluff was incorrectly reduced");

  /* Facing a three-quarter-pot bet, a 3x raise asks the bettor to call 150
   * into a final pot of 550: 27%, not the opening-bet formula's 36%. */
  state.position = FELT_POSITION_BUTTON;
  state.pot = 175;
  state.to_call = 75;
  state.opp_street_contribution = 75;
  require(felt_balanced_bluff_frequency(&state, 225, false) == 13,
          "3x raise used opening-bet bluff math");
  require(felt_balanced_bluff_frequency(&state, 225, true) == 13,
          "in-position pure-air raise did not realize half its base share");
  state.position = FELT_POSITION_BIG_BLIND;
  require(felt_balanced_bluff_frequency(&state, 225, true) == 4,
          "out-of-position pure-air raise did not realize fifteen percent");

  state.street = FELT_STREET_RIVER;
  state.position = FELT_POSITION_BUTTON;
  require(felt_balanced_bluff_frequency(&state, 225, true) == 17,
          "river did not increase in-position pure-air raise realization");
  state.position = FELT_POSITION_BIG_BLIND;
  require(felt_balanced_bluff_frequency(&state, 225, true) == 17,
          "river retained an out-of-position bluff-realization penalty");

  state.street = FELT_STREET_FLOP;
  state.position = FELT_POSITION_BUTTON;
  state.pot = 400;
  state.my_street_contribution = 75;
  state.opp_street_contribution = 225;
  state.to_call = 150;
  require(felt_balanced_bluff_frequency(&state, 675, true) == 1,
          "pure-air re-raise did not realize five percent of its base share");
}

void test_later_barrels_need_more_value() {
  FeltHandValue value{};
  value.valid = true;
  value.points = 35;  // delta -15 against a neutral range

  FeltRangeRead read{};
  read.valid = true;
  read.score = 50;
  read.polarisation = 20;

  FeltGameState flop{};
  flop.street = FELT_STREET_FLOP;
  flop.position = FELT_POSITION_BUTTON;
  flop.decision_random = 0;
  require(felt_value_raise(&flop, &value, &read).raise,
          "first-barrel thin bet did not use the calibrated score threshold");

  Hand hand;
  hand.blinds();
  hand.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  hand.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  hand.add(FELT_POSITION_BUTTON, FELT_STREET_FLOP, FELT_EVENT_BET, 330);
  hand.add(FELT_POSITION_BIG_BLIND, FELT_STREET_FLOP, FELT_EVENT_CALL, 330);
  hand.add(FELT_POSITION_BUTTON, FELT_STREET_TURN, FELT_EVENT_BET, 800);
  hand.add(FELT_POSITION_BIG_BLIND, FELT_STREET_TURN, FELT_EVENT_CALL, 800);
  FeltGameState river = hand.state(FELT_STREET_RIVER, 2760, 0, kNoBet);
  river.position = FELT_POSITION_BUTTON;
  river.decision_random = 0;
  require(!felt_value_raise(&river, &value, &read).raise,
          "two prior barrels did not tighten the river betting threshold");
}

/* Rank-sensitive high card should feed the ordinary call rules; it does not
 * need a second special-case policy. Against the same first bet, ace-king is
 * inside the widened thin band while ten-three remains outside it. */
void test_high_card_naturally_enters_thin_call_band() {
  FeltGameState state{};
  state.street = FELT_STREET_RIVER;
  state.position = FELT_POSITION_BUTTON;
  state.pot = 700;
  state.to_call = 300;
  state.opp_street_contribution = 300;
  state.decision_random = 0;

  const std::vector<FeltCard> board = {
      card(9, 2), card(7, 1), card(4, 3), card(2, 0), card(0, 1)};
  const FeltHandValue strong =
      value_of(state, {card(12, 0), card(11, 1)}, board);
  const FeltHandValue weak =
      value_of(state, {card(8, 0), card(1, 2)}, board);

  FeltRangeRead read{};
  read.valid = true;
  read.score = 26;
  read.bluff_rate_basis_points = 2000;
  FeltDraws no_draw{};
  no_draw.valid = true;

  require(felt_range_delta(&strong, &read) >= -30.0 &&
              felt_range_delta(&weak, &read) < -30.0,
          "high-card scores did not straddle the thin-call boundary");
  require(felt_should_call(&state, &strong, &read, &no_draw),
          "strong high card did not naturally bluff-catch");
  require(!felt_should_call(&state, &weak, &read, &no_draw),
          "weak high card entered the bluff-catching range");
}

void test_out_of_position_value_raise_threshold() {
  FeltGameState state{};
  state.street = FELT_STREET_FLOP;
  state.to_call = 75;

  FeltHandValue value{};
  value.valid = true;
  value.points = 75;

  FeltRangeRead read{};
  read.valid = true;
  read.score = 60;  // adjusted score 70, delta 20
  read.polarisation = 20;
  read.hero_out_of_position = false;
  require(felt_value_raise(&state, &value, &read).raise,
          "in-position merged value hand did not raise at delta 20");
  read.hero_out_of_position = true;
  require(!felt_value_raise(&state, &value, &read).raise,
          "out-of-position value raise ignored its tighter threshold");
}

void test_reactive_bluff_candidates() {
  FeltGameState state{};
  state.street = FELT_STREET_FLOP;
  state.position = FELT_POSITION_BUTTON;
  state.pot = 1000;
  state.to_call = 300;
  state.my_stack = 5000;
  state.opp_stack = 5000;
  state.opp_street_contribution = 300;

  FeltHandValue value{};
  value.valid = true;
  value.points = 4;
  value.made_points = 4;

  FeltRangeRead read{};
  read.valid = true;
  read.score = 40;

  FeltDraws weak{};
  weak.valid = true;
  weak.straight_next_cards = 3;
  require(!felt_bluff_opportunity(&state, &value, &read, &weak).valid,
          "weak draw raised a bet instead of using call pricing");

  FeltDraws strong = weak;
  strong.straight_next_cards = 8;
  const FeltBluffOpportunity strong_plan =
      felt_bluff_opportunity(&state, &value, &read, &strong);
  require(strong_plan.valid && !strong_plan.pure_air,
          "eight-out draw was not eligible to semi-bluff raise");

  state.my_street_contribution = 100;
  state.opp_street_contribution = 300;
  state.to_call = 200;
  FeltDraws none{};
  none.valid = true;
  const FeltBluffOpportunity reraise =
      felt_bluff_opportunity(&state, &value, &read, &none);
  require(reraise.valid && reraise.pure_air,
          "pure air was not eligible for its rare re-raise");
}

/*
 * The board belongs to whoever raised before the flop. The same line scores
 * differently on a king-high flop and a seven-high one, and in opposite
 * directions depending on which of the two the opponent was.
 */
void test_range_advantage() {
  const std::uint32_t them = FELT_POSITION_BUTTON;
  const std::vector<FeltCard> broadway = {card(11, 2), card(9, 1), card(2, 3)};
  const std::vector<FeltCard> low = {card(5, 2), card(3, 1), card(1, 3)};
  const FeltBoardTexture high_texture =
      felt_board_texture(broadway.data(), 3U);
  const FeltBoardTexture low_texture = felt_board_texture(low.data(), 3U);

  /* They raised preflop, we called, and they have bet the flop. */
  Hand theirs;
  theirs.blinds();
  theirs.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  theirs.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  theirs.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 330);
  const FeltGameState theirs_state =
      theirs.state(FELT_STREET_FLOP, 830, 330, kAll);
  const int theirs_high = felt_read_range(&theirs_state, &high_texture).score;
  const int theirs_low = felt_read_range(&theirs_state, &low_texture).score;
  require(theirs_high > theirs_low,
          "the raiser's range did not gain on a broadway board");

  /* We raised preflop, they called, and they have bet the flop. */
  Hand ours;
  ours.blinds();
  ours.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  ours.add(them, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  ours.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 330);
  const FeltGameState ours_state = ours.state(FELT_STREET_FLOP, 830, 330, kAll);
  const int caller_high = felt_read_range(&ours_state, &high_texture).score;
  const int caller_low = felt_read_range(&ours_state, &low_texture).score;
  require(caller_low > caller_high,
          "the caller's range did not gain on a low board");

  /* A limped pot separates nobody, so the board decides nothing. */
  Hand limped;
  limped.blinds();
  limped.add(them, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 100);
  limped.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CHECK, 100);
  limped.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 130);
  const FeltGameState limped_state =
      limped.state(FELT_STREET_FLOP, 330, 130, kAll);
  require(felt_read_range(&limped_state, &high_texture).score ==
              felt_read_range(&limped_state, &low_texture).score,
          "a limped pot gave one side the board");
}

/*
 * Each preflop raise narrows a range, and not by the same amount each time.
 *
 * The four fixtures have the opponent raise our flop bet rather than bet into
 * us, because a bet is no longer one thing: the preflop raiser's own barrels
 * are scored apart from a bet by someone who did not raise, so a limped-pot
 * bet and a single-raised-pot continuation bet are not comparable and a
 * ladder built from them measures two effects at once. A raise is a raise in
 * every pot, which leaves only the term under test.
 */
void test_preflop_ladder() {
  const std::uint32_t them = FELT_POSITION_BUTTON;
  const std::uint32_t us = FELT_POSITION_BIG_BLIND;
  const std::vector<FeltCard> board = {card(11, 2), card(9, 1), card(2, 3)};
  const FeltBoardTexture texture = felt_board_texture(board.data(), 3U);

  Hand limped;
  limped.blinds();
  limped.add(them, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 100);
  limped.add(us, FELT_STREET_PREFLOP, FELT_EVENT_CHECK, 100);
  limped.add(us, FELT_STREET_FLOP, FELT_EVENT_BET, 130);
  limped.add(them, FELT_STREET_FLOP, FELT_EVENT_RAISE, 390);
  const FeltGameState limped_state =
      limped.state(FELT_STREET_FLOP, 720, 260, kAll, 130);

  Hand opened;
  opened.blinds();
  opened.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  opened.add(us, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  opened.add(us, FELT_STREET_FLOP, FELT_EVENT_BET, 330);
  opened.add(them, FELT_STREET_FLOP, FELT_EVENT_RAISE, 990);
  const FeltGameState opened_state =
      opened.state(FELT_STREET_FLOP, 1820, 660, kAll, 330);

  Hand three_bet;
  three_bet.blinds();
  three_bet.add(us, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  three_bet.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 900);
  three_bet.add(us, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 900);
  three_bet.add(us, FELT_STREET_FLOP, FELT_EVENT_BET, 1200);
  three_bet.add(them, FELT_STREET_FLOP, FELT_EVENT_RAISE, 3600);
  const FeltGameState three_bet_state =
      three_bet.state(FELT_STREET_FLOP, 6600, 2400, kAll, 1200);

  Hand four_bet;
  four_bet.blinds();
  four_bet.add(us, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  four_bet.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 900);
  four_bet.add(us, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 2600);
  four_bet.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 7000);
  four_bet.add(us, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 7000);
  four_bet.add(us, FELT_STREET_FLOP, FELT_EVENT_BET, 9000);
  four_bet.add(them, FELT_STREET_FLOP, FELT_EVENT_RAISE, 27000);
  const FeltGameState four_bet_state =
      four_bet.state(FELT_STREET_FLOP, 50000, 18000, kAll, 9000);

  const int limp = felt_read_range(&limped_state, &texture).score;
  const int open = felt_read_range(&opened_state, &texture).score;
  const int three = felt_read_range(&three_bet_state, &texture).score;
  const int four = felt_read_range(&four_bet_state, &texture).score;

  require(limp < open && open < three && three < four,
          "the preflop ladder is not monotonic: " + std::to_string(limp) +
              " " + std::to_string(open) + " " + std::to_string(three) + " " +
              std::to_string(four));
  require(three - open >= 12,
          "a three-bet was worth only " + std::to_string(three - open) +
              " points more than an open");
  require(four - three >= 10,
          "a four-bet was worth only " + std::to_string(four - three) +
              " points more than a three-bet");
  require(four - open >= 26,
          "a four-bet was only " + std::to_string(four - open) +
              " points stronger than a single raise");
}

/*
 * A barrel is scored by how many streets it has been fired on. The first one
 * is the whole raising range and claims barely more than a check; each one
 * after has given up on some of the hands that missed.
 */
void test_barrel_ladder() {
  const std::uint32_t them = FELT_POSITION_BUTTON;
  const std::vector<FeltCard> board = {card(11, 2), card(9, 1), card(2, 3),
                                       card(5, 0), card(0, 1)};
  const FeltBoardTexture flop_texture = felt_board_texture(board.data(), 3U);

  Hand hand;
  hand.blinds();
  hand.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  hand.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  hand.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 330);
  const FeltGameState flop = hand.state(FELT_STREET_FLOP, 830, 330, kAll);
  const int first = felt_read_range(&flop, &flop_texture).score;

  hand.add(FELT_POSITION_BIG_BLIND, FELT_STREET_FLOP, FELT_EVENT_CALL, 330);
  hand.add(them, FELT_STREET_TURN, FELT_EVENT_BET, 800);
  FeltGameState turn = hand.state(FELT_STREET_TURN, 2960, 800, kAll);
  set_board(turn, {board[0], board[1], board[2], board[3]});
  const FeltBoardTexture turn_texture = felt_board_texture(board.data(), 4U);
  const int second = felt_read_range(&turn, &turn_texture).score;

  hand.add(FELT_POSITION_BIG_BLIND, FELT_STREET_TURN, FELT_EVENT_CALL, 800);
  hand.add(them, FELT_STREET_RIVER, FELT_EVENT_BET, 1900);
  FeltGameState river = hand.state(FELT_STREET_RIVER, 7060, 1900, kAll);
  set_board(river, board);
  const FeltBoardTexture river_texture = felt_board_texture(board.data(), 5U);
  const int third = felt_read_range(&river, &river_texture).score;

  require(first < second && second < third,
          "barrels do not climb: " + std::to_string(first) + " " +
              std::to_string(second) + " " + std::to_string(third));

  /* A bet from someone who did not raise before the flop is a narrower
   * action than the raiser's routine continuation bet. */
  Hand donk;
  donk.blinds();
  donk.add(them, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 100);
  donk.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CHECK, 100);
  donk.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 130);
  const FeltGameState donk_state = donk.state(FELT_STREET_FLOP, 330, 130, kAll);
  require(felt_read_range(&donk_state, &flop_texture).score > first,
          "a continuation bet should claim less than a bet from a limper");
}

/* Repeat the geometric size on every street and the stack lands on zero. */
void test_geometric_sizing() {
  require(felt_geometric_bet_percent(0, 100, FELT_STREET_FLOP) == 0,
          "sized a pot of nothing");

  FeltChips pot = 1500;
  FeltChips stack = 19250;
  for (std::uint32_t street = FELT_STREET_FLOP; street <= FELT_STREET_RIVER;
       street++) {
    const int percent = felt_geometric_bet_percent(pot, stack, street);
    FeltChips bet = pot * percent / 100;
    if (bet > stack) bet = stack;
    pot += 2 * bet;
    stack -= bet;
  }
  require(stack >= 0 && stack < 1500 / 10,
          "three geometric bets left " + std::to_string(stack) +
              " behind instead of getting the stack in");

  /* Shallow pots need small bets, not the same fraction. */
  require(felt_geometric_bet_percent(20000, 4000, FELT_STREET_FLOP) <
              felt_geometric_bet_percent(1500, 19250, FELT_STREET_FLOP),
          "the size did not shrink as the stack-to-pot ratio fell");
}

/* The same hand, against two different lines. */
void test_same_hand_two_ranges(felt::NativeBotRunner& bot) {
  const std::uint32_t them = FELT_POSITION_BUTTON;
  const std::vector<FeltCard> board = {card(11, 2), card(5, 1), card(2, 3)};

  Hand quiet;
  quiet.blinds();
  quiet.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  quiet.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  quiet.add(them, FELT_STREET_FLOP, FELT_EVENT_CHECK, 0);
  {
    /* Top pair, and they have shown nothing: bet. */
    FeltGameState state = quiet.state(FELT_STREET_FLOP, 500, 0, kNoBet);
    state.hole[0] = card(11, 0);
    state.hole[1] = card(9, 1);
    set_board(state, board);
    require(bot.act(state).type == FELT_ACTION_RAISE_TO,
            "checked top pair back against a range that had shown nothing");
  }

  Hand war;
  war.blinds();
  war.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  war.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  war.add(FELT_POSITION_BIG_BLIND, FELT_STREET_FLOP, FELT_EVENT_BET, 330);
  war.add(them, FELT_STREET_FLOP, FELT_EVENT_RAISE, 1200);
  war.add(FELT_POSITION_BIG_BLIND, FELT_STREET_FLOP, FELT_EVENT_RAISE, 3000);
  war.add(them, FELT_STREET_FLOP, FELT_EVENT_RAISE, 8000);
  {
    /* The identical hand, three raises later: gone. */
    FeltGameState state = war.state(FELT_STREET_FLOP, 11500, 5000, kAll);
    state.my_street_contribution = 3000;
    state.hole[0] = card(11, 0);
    state.hole[1] = card(9, 1);
    set_board(state, board);
    require(bot.act(state).type == FELT_ACTION_FOLD,
            "paid off a range that had raised three times with one pair");
  }
}

void test_tiny_bet_preempts_air_bluff(felt::NativeBotRunner& bot) {
  Hand hand;
  hand.blinds();
  hand.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  hand.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  hand.add(FELT_POSITION_BIG_BLIND, FELT_STREET_FLOP, FELT_EVENT_CHECK, 0);
  hand.add(FELT_POSITION_BUTTON, FELT_STREET_FLOP, FELT_EVENT_BET, 40);

  FeltGameState state = hand.state(FELT_STREET_FLOP, 540, 40, kAll);
  state.hole[0] = card(4, 0);
  state.hole[1] = card(1, 1);
  set_board(state, {card(10, 2), card(7, 1), card(0, 3)});
  state.decision_random = 0;
  require(bot.act(state).type == FELT_ACTION_CALL,
          "turned a mandatory sub-ten-percent call into an air bluff");
}

/* Weak ranges get bluffed at more often than strong ones. */
void test_bluff_frequency_tracks_the_range(felt::NativeBotRunner& bot) {
  const std::uint32_t them = FELT_POSITION_BUTTON;
  const std::vector<FeltCard> board = {card(11, 2), card(8, 1), card(3, 3)};
  const int kTrials = 3000;

  auto rate = [&](const Hand& hand, FeltChips pot, FeltChips to_call,
                  std::uint32_t legal, FeltChips mine) {
    int raised = 0;
    for (int trial = 0; trial < kTrials; trial++) {
      FeltGameState state = hand.state(FELT_STREET_FLOP, pot, to_call, legal);
      state.my_street_contribution = mine;
      state.hole[0] = card(4, 0);
      state.hole[1] = card(1, 1);
      set_board(state, board);
      state.decision_random = static_cast<std::uint64_t>(trial) * 2654435761ULL;
      if (bot.act(state).type == FELT_ACTION_RAISE_TO) raised++;
    }
    return 100.0 * raised / kTrials;
  };

  Hand limped;
  limped.blinds();
  limped.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 100);
  limped.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CHECK, 100);
  limped.add(them, FELT_STREET_FLOP, FELT_EVENT_CHECK, 0);
  const double weak = rate(limped, 200, 0, kNoBet, 0);

  Hand strong;
  strong.blinds();
  strong.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 300);
  strong.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 900);
  strong.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 2400);
  strong.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 2400);
  strong.add(FELT_POSITION_BIG_BLIND, FELT_STREET_FLOP, FELT_EVENT_BET, 1600);
  strong.add(them, FELT_STREET_FLOP, FELT_EVENT_RAISE, 5200);
  const double tough = rate(strong, 11000, 3600, kAll, 1600);

  if (weak <= tough) {
    throw std::runtime_error(
        "bluffed a strong range at least as often as a weak one: " +
        std::to_string(weak) + "% versus " + std::to_string(tough) + "%");
  }
  if (weak < 8.0) {
    throw std::runtime_error("bluffed a range that showed nothing only " +
                             std::to_string(weak) + "% of the time");
  }
  if (tough > 18.0) {
    throw std::runtime_error("bluffed a range that raised twice " +
                             std::to_string(tough) + "% of the time");
  }
}

/* Polarisation asks a different question from strength, and answers it from
 * the bet size and the board rather than from the line alone. */
void test_polarisation() {
  const std::uint32_t them = FELT_POSITION_BUTTON;
  const std::vector<FeltCard> dry = {card(12, 0), card(8, 1), card(0, 2)};
  const std::vector<FeltCard> four_flush = {card(11, 2), card(8, 2), card(4, 2),
                                            card(2, 2)};
  const std::vector<FeltCard> wet_live = {card(9, 2), card(8, 1), card(7, 2)};
  const FeltBoardTexture dry_texture = felt_board_texture(dry.data(), 3U);
  const FeltBoardTexture flush_texture =
      felt_board_texture(four_flush.data(), 4U);
  const FeltBoardTexture live_texture = felt_board_texture(wet_live.data(), 3U);

  Hand opened;
  opened.blinds();
  opened.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  opened.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);

  Hand small_bet = opened;
  small_bet.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 150);
  const FeltGameState small_state =
      small_bet.state(FELT_STREET_FLOP, 650, 150, kAll);

  Hand big_bet = opened;
  big_bet.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 750);
  const FeltGameState big_state =
      big_bet.state(FELT_STREET_FLOP, 1250, 750, kAll);

  require(felt_read_range(&big_state, &live_texture).polarisation >
              felt_read_range(&small_state, &live_texture).polarisation,
          "a big bet was not read as more polarised than a small one");

  Hand bet = opened;
  bet.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 330);
  const FeltGameState bet_state = bet.state(FELT_STREET_FLOP, 830, 330, kAll);
  const int on_dry = felt_read_range(&bet_state, &dry_texture).polarisation;
  const int on_flush = felt_read_range(&bet_state, &flush_texture).polarisation;
  const int on_live = felt_read_range(&bet_state, &live_texture).polarisation;
  require(on_dry > on_live,
          "a bet into a dry board was not read as more polarised than one "
          "into a live one");
  require(on_flush > on_live,
          "a bet into four to a flush was not read as more polarised");

  Hand checked = opened;
  checked.add(them, FELT_STREET_FLOP, FELT_EVENT_CHECK, 0);
  const FeltGameState checked_state =
      checked.state(FELT_STREET_FLOP, 500, 0, kNoBet);
  require(felt_read_range(&checked_state, &live_texture).polarisation <
              FELT_POLARISED_AT,
          "a range that only checked was read as polarised");
}

/* Two sizes per intent, and the weight between them is where the board, the
 * outs and the plan all get a say. */
void test_sizing_pairs() {
  const std::uint32_t them = FELT_POSITION_BUTTON;
  const std::vector<FeltCard> board = {card(9, 2), card(8, 1), card(7, 2)};
  const FeltBoardTexture texture = felt_board_texture(board.data(), 3U);
  const FeltBoardTexture neutral{};
  const FeltDraws none{};

  Hand opened;
  opened.blinds();
  opened.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  opened.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  opened.add(them, FELT_STREET_FLOP, FELT_EVENT_CHECK, 0);
  const FeltGameState state = opened.state(FELT_STREET_FLOP, 500, 0, kNoBet);
  FeltRangeRead read = felt_read_range(&state, &texture);
  read.street_aggression = 1U;

  read.polarisation = 80;
  const FeltSizing polarised_value =
      felt_choose_size(&state, &read, &neutral, &none, FELT_SIZING_VALUE,
                       false, 0);
  require(polarised_value.small == 0.33 && polarised_value.large == 0.66 &&
              polarised_value.weight_large == 30,
          "polarised bet row was not 0.33/0.66 at 30 percent large");

  read.polarisation = 20;
  const FeltSizing merged_value =
      felt_choose_size(&state, &read, &neutral, &none, FELT_SIZING_VALUE,
                       false, 0);
  require(merged_value.small == 0.66 && merged_value.large == 1.25 &&
              merged_value.weight_large == 45,
          "merged bet row was not 0.66/1.25 at 45 percent large");

  const FeltSizing thin =
      felt_choose_size(&state, &read, &neutral, &none, FELT_SIZING_THIN_VALUE,
                       false, 0);
  require(thin.small == 0.5 && thin.large == 0.5,
          "merged thin value was not the single half-pot size");

  FeltGameState raised_state = state;
  raised_state.pot = 1400;
  raised_state.to_call = 600;
  raised_state.my_street_contribution = 200;
  raised_state.opp_street_contribution = 800;
  const FeltSizing reraise =
      felt_choose_size(&raised_state, &read, &texture, &none,
                       FELT_SIZING_VALUE, true, 0);
  require(reraise.small == 2.5 && reraise.large == 3.0 &&
              reraise.relative_to_opponent,
          "re-raise row was not 2.5x/3x their raise");

  /* The geometric plan pulls the weight toward whichever size is nearer. */
  read.polarisation = 20;
  const FeltSizing near_small =
      felt_choose_size(&state, &read, &texture, &none, FELT_SIZING_VALUE,
                       false, 60);
  const FeltSizing near_large =
      felt_choose_size(&state, &read, &texture, &none, FELT_SIZING_VALUE,
                       false, 130);
  require(near_large.weight_large > near_small.weight_large,
          "the geometric size did not pull toward the nearer candidate");
}

/* Every size, betting or re-raising, should be reachable more than one way,
 * so the size on its own gives nothing away. */
void test_sizes_overlap() {
  const std::vector<FeltCard> board = {card(9, 2), card(8, 1), card(7, 2)};
  const FeltBoardTexture texture = felt_board_texture(board.data(), 3U);
  const FeltDraws none{};
  Hand hand;
  hand.blinds();
  hand.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  hand.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  const FeltGameState state = hand.state(FELT_STREET_FLOP, 500, 0, kNoBet);

  std::vector<double> seen;
  for (int polarisation : {80, 20}) {
    for (bool facing_raise : {false, true}) {
      for (FeltSizingIntent intent :
           {FELT_SIZING_VALUE, FELT_SIZING_THIN_VALUE, FELT_SIZING_BLUFF}) {
        if (facing_raise && intent == FELT_SIZING_THIN_VALUE) continue;
        FeltRangeRead read = felt_read_range(&state, &texture);
        read.polarisation = polarisation;
        const FeltSizing sizing = felt_choose_size(
            &state, &read, &texture, &none, intent, facing_raise, 0);
        seen.push_back(sizing.small);
        seen.push_back(sizing.large);
      }
    }
  }
  for (double size : seen) {
    int count = 0;
    for (double other : seen) {
      if (other > size - 0.01 && other < size + 0.01) count++;
    }
    if (count < 2) {
      throw std::runtime_error(
          "the size " + std::to_string(size) +
          " appears in only one branch, so using it gives the hand away");
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "expected the the_crusher bot library path\n";
    return 2;
  }
  try {
    felt_bot_kit_warmup();
    test_range_score();
    test_adjusted_score_and_bluff_estimate();
    test_large_bet_raise_penalty();
    test_street_and_position_bluff_catch();
    test_preflop_actor_model();
    test_position_aware_unopened_range();
    test_cheap_call_rules();
    test_balanced_bluff_frequency();
    test_later_barrels_need_more_value();
    test_high_card_naturally_enters_thin_call_band();
    test_out_of_position_value_raise_threshold();
    test_reactive_bluff_candidates();
    test_range_advantage();
    test_preflop_ladder();
    test_barrel_ladder();
    test_polarisation();
    test_sizing_pairs();
    test_sizes_overlap();
    test_geometric_sizing();
    felt::NativeBotRunner bot(argv[1]);
    test_same_hand_two_ranges(bot);
    test_tiny_bet_preempts_air_bluff(bot);
    test_bluff_frequency_tracks_the_range(bot);
  } catch (const std::exception& error) {
    std::cerr << "crusher_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

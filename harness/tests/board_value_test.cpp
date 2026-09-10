#include "felt/bot_api.h"
#include "felt/bot_kit.h"
#include "felt/native_bot_runner.hpp"

#include "board_value.h"

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

FeltHandValue value_of(std::array<FeltCard, 2> hole,
                       std::vector<FeltCard> board) {
  FeltGameState state{};
  state.hole[0] = hole[0];
  state.hole[1] = hole[1];
  state.board_count = static_cast<std::uint8_t>(board.size());
  state.street = board.size() == 3U ? FELT_STREET_FLOP
                 : board.size() == 4U ? FELT_STREET_TURN
                                      : FELT_STREET_RIVER;
  for (std::size_t index = 0; index < board.size(); ++index) {
    state.board[index] = board[index];
  }
  const FeltMadeHand made =
      felt_made_hand(hole.data(), board.data(),
                     static_cast<std::uint8_t>(board.size()));
  const FeltDraws draws = felt_draws(
      hole.data(), board.data(), static_cast<std::uint8_t>(board.size()));
  const FeltBoardTexture texture = felt_board_texture(
      board.data(), static_cast<std::uint8_t>(board.size()));
  return felt_board_relative_value(&state, &made, &draws, &texture);
}

void expect_points(std::array<FeltCard, 2> hole,
                   std::vector<FeltCard> board,
                   int expected,
                   const std::string& what) {
  const FeltHandValue value = value_of(hole, board);
  require(value.valid, what + ": scoring failed");
  require(value.points == expected,
          what + ": scored " + std::to_string(value.points) + ", wanted " +
              std::to_string(expected));
}

const char* band_name(FeltHandBand band) {
  switch (band) {
    case FELT_BAND_AIR: return "air";
    case FELT_BAND_MARGINAL: return "marginal";
    case FELT_BAND_MEDIUM: return "medium";
    case FELT_BAND_STRONG: return "strong";
    default: return "nutted";
  }
}

void expect_band(std::array<FeltCard, 2> hole,
                 std::vector<FeltCard> board,
                 FeltHandBand expected,
                 const std::string& what) {
  const FeltHandValue value = value_of(hole, board);
  require(value.valid, what + ": scoring failed");
  if (value.band != expected) {
    throw std::runtime_error(what + ": scored " +
                             std::to_string(value.points) + " (" +
                             band_name(value.band) + "), wanted " +
                             band_name(expected));
  }
}

/* The board changes what a hand is worth, which is the whole point. */
void test_board_relative_value() {
  const std::array<FeltCard, 2> king_queen = {card(11, 0), card(10, 1)};

  /* Top pair, dry rainbow board. */
  expect_band(king_queen, {card(11, 2), card(5, 1), card(2, 3)},
              FELT_BAND_STRONG, "top pair on a rainbow board");

  /* The same top pair with four to a flush out there. */
  expect_band(king_queen, {card(11, 2), card(5, 2), card(2, 2), card(7, 2)},
              FELT_BAND_MARGINAL, "top pair into four to a flush");

  /* Only three cards occupy one five-rank window here, so the table charges
   * four points and the strong-kicker top pair remains just strong. */
  expect_band(king_queen, {card(11, 2), card(4, 1), card(3, 3), card(2, 0)},
              FELT_BAND_STRONG, "top pair into a connected board");

  /* A set is nutted on a dry board and merely strong on a wet one. */
  const std::array<FeltCard, 2> sevens = {card(5, 0), card(5, 1)};
  expect_band(sevens, {card(5, 2), card(11, 1), card(2, 3)},
              FELT_BAND_NUTTED, "a set on a dry board");
  expect_band(sevens, {card(5, 2), card(11, 2), card(2, 2), card(9, 2)},
              FELT_BAND_STRONG, "a set into four to a flush");

  /* Weak pairs and nothing. */
  expect_band(king_queen, {card(12, 3), card(10, 2), card(2, 1)},
              FELT_BAND_MEDIUM, "middle pair with a strong kicker");
  expect_band(king_queen, {card(8, 3), card(6, 2), card(2, 1)},
              FELT_BAND_MARGINAL, "king-queen high");

  /* A made flush is not frightened by the three suited cards making it. */
  const std::array<FeltCard, 2> suited = {card(12, 2), card(9, 2)};
  expect_band(suited, {card(6, 2), card(4, 2), card(2, 2)},
              FELT_BAND_NUTTED, "a made flush");
}

void test_score_table_edge_cases() {
  /* Kicker changes top pair within its 40..48 bucket and is classified. */
  const FeltHandValue strong_top = value_of(
      {card(11, 0), card(10, 1)},
      {card(11, 2), card(5, 1), card(2, 3)});
  const FeltHandValue weak_top = value_of(
      {card(11, 0), card(1, 1)},
      {card(11, 2), card(5, 1), card(2, 3)});
  require(strong_top.kicker == FELT_KICKER_STRONG &&
              weak_top.kicker == FELT_KICKER_WEAK &&
              strong_top.points > weak_top.points,
          "top-pair kicker bands did not change its score");

  /* Small hands use the same idea as flushes and full houses: count the
   * better ranks still available rather than treating every member of the
   * class alike. Both hole cards matter to high card; the kicker separates
   * otherwise identical one-pair classes. */
  const std::vector<FeltCard> high_board = {
      card(9, 2), card(7, 1), card(4, 3), card(2, 0), card(0, 1)};
  const FeltHandValue ace_king_high =
      value_of({card(12, 0), card(11, 1)}, high_board);
  const FeltHandValue ten_three_high =
      value_of({card(8, 0), card(1, 2)}, high_board);
  require(ace_king_high.made_points > ten_three_high.made_points &&
              ace_king_high.made_points <= 16 &&
              ace_king_high.made_points < 17,
          "high-card ranks did not form a bounded strength ladder");

  const FeltHandValue middle_ace = value_of(
      {card(7, 0), card(12, 1)},
      {card(11, 2), card(7, 1), card(2, 3)});
  const FeltHandValue middle_three = value_of(
      {card(7, 0), card(1, 1)},
      {card(11, 2), card(7, 1), card(2, 3)});
  require(middle_ace.made_points > middle_three.made_points,
          "middle-pair kicker did not change its score");

  const FeltHandValue board_pair_ace = value_of(
      {card(12, 0), card(10, 1)},
      {card(5, 2), card(5, 1), card(2, 3)});
  const FeltHandValue board_pair_four = value_of(
      {card(2, 0), card(1, 1)},
      {card(5, 2), card(5, 1), card(0, 3)});
  require(board_pair_ace.made_points > board_pair_four.made_points,
          "board-pair kicker did not change its score");

  /* The paired-board threat is already present in these hand classes and
   * must not be charged a second time. */
  expect_points({card(12, 0), card(12, 1)},
                {card(11, 2), card(11, 3), card(10, 0), card(5, 1), card(2, 3)},
                68, "over two pair on a paired board");
  expect_points({card(12, 0), card(11, 1)},
                {card(12, 2), card(11, 3), card(5, 0), card(5, 1), card(0, 3)},
                55, "two private pairs threatened by a third board pair");
  expect_points({card(5, 0), card(12, 1)},
                {card(5, 2), card(5, 3), card(11, 0)},
                74, "trips using the board pair with a strong kicker");
  expect_points({card(5, 0), card(0, 1)},
                {card(5, 2), card(5, 3), card(11, 0)},
                68, "trips using the board pair with a weak kicker");

  /* A two-pair board weakens a completed full house; a board straight is
   * shared, while a private straight on four connected cards is merely less
   * exclusive. */
  expect_points({card(12, 0), card(3, 1)},
                {card(12, 2), card(12, 3), card(11, 0), card(11, 1), card(0, 2)},
                86, "full house completed on a two-pair board");
  expect_points({card(0, 0), card(1, 1)},
                {card(12, 2), card(11, 1), card(10, 3), card(9, 0), card(8, 2)},
                56, "straight playing the board");
  expect_points({card(8, 0), card(12, 1)},
                {card(7, 2), card(6, 1), card(5, 3), card(4, 0)},
                73, "private straight on a four-straight board");

  /*
   * Trips on the board are not our trips. This used to score 74, one point
   * under a set, and the crusher value-bet an ace-high kicker on 888 for
   * three streets in match 278 hand 16174 -- and on QQQ with five-three it
   * bet three streets holding literally nothing (hand 10092). The class is
   * common property; only the kicker is ours, and an ace is the best kicker
   * there is, so this is the top of a narrow band rather than a strong hand.
   */
  expect_points({card(12, 0), card(1, 1)},
                {card(5, 2), card(5, 3), card(5, 0), card(11, 1), card(3, 2)},
                32, "trips on board with an ace kicker");
  const FeltHandValue board_trips_weak = value_of(
      {card(4, 0), card(1, 1)},
      {card(5, 2), card(5, 3), card(5, 0), card(11, 1), card(3, 2)});
  require(board_trips_weak.points < 32,
          "a weak kicker on a trips board should score under an ace kicker");
  const FeltHandValue shared_quads = value_of(
      {card(12, 0), card(1, 1)},
      {card(5, 2), card(5, 3), card(5, 0), card(5, 1), card(3, 2)});
  require(shared_quads.points == 82 &&
              shared_quads.kicker == FELT_KICKER_STRONG,
          "quads on board did not use the private kicker");

  /* A four-flush is one 20-point penalty, never 20 plus the three-flush 7. */
  const FeltHandValue four_flush = value_of(
      {card(11, 0), card(10, 1)},
      {card(11, 2), card(5, 2), card(2, 2), card(7, 2)});
  require(four_flush.board_penalty == 20,
          "four-flush penalty stacked with the three-flush penalty");

  const FeltHandValue private_pair = value_of(
      {card(11, 0), card(10, 1)},
      {card(11, 2), card(5, 1), card(2, 3)});
  const FeltHandValue board_pair = value_of(
      {card(10, 0), card(9, 1)},
      {card(11, 2), card(11, 1), card(2, 3)});
  const FeltHandValue board_two_pair = value_of(
      {card(10, 0), card(9, 1)},
      {card(11, 2), card(11, 1), card(2, 3), card(2, 0)});
  require(private_pair.player_made_pair_or_better,
          "a pair made with a hole card was not marked private");
  require(!board_pair.player_made_pair_or_better &&
              !board_two_pair.player_made_pair_or_better,
          "a board-made pair or two pair was marked as the player's hand");
}

FeltGameState price_state(std::uint32_t street,
                          FeltChips pot,
                          FeltChips to_call,
                          std::uint32_t legal) {
  FeltGameState state{};
  state.abi_version = FELT_BOT_ABI_VERSION;
  state.struct_size = sizeof(FeltGameState);
  state.street = street;
  state.position = FELT_POSITION_BUTTON;
  state.pot = pot;
  state.to_call = to_call;
  state.my_stack = 20000;
  state.opp_stack = 20000;
  state.opp_street_contribution = to_call;
  state.min_raise_to = to_call * 2 > 100 ? to_call * 2 : 100;
  state.max_raise_to = 20000;
  state.legal_actions = legal;
  /* A seed that trips none of the mixes, so the cases below exercise the
   * deterministic path. Zero would fire every one of them. */
  state.decision_random = UINT64_C(0x01000000);
  std::memset(state.board, FELT_INVALID_CARD, sizeof(state.board));
  return state;
}

/* Price is the share of the pot the call would build, not of the pot now. */
void test_call_price() {
  require(felt_call_price_percent(nullptr) == 0, "null state priced");
  FeltGameState free_check = price_state(FELT_STREET_FLOP, 400, 0, kNoBet);
  require(felt_call_price_percent(&free_check) == 0, "a check has a price");

  FeltGameState third = price_state(FELT_STREET_FLOP, 400, 133, kAll);
  require(felt_call_price_percent(&third) == 24,
          "a third-pot bet should cost 24 percent of the pot it makes");

  FeltGameState full = price_state(FELT_STREET_FLOP, 400, 400, kAll);
  require(felt_call_price_percent(&full) == 50,
          "a pot-sized bet should cost half the pot it makes");
}

/* Four percent an out on the flop, two on the turn. */
void test_draw_price() {
  const std::array<FeltCard, 2> suited = {card(12, 2), card(9, 2)};
  const std::vector<FeltCard> flop = {card(6, 2), card(4, 2), card(11, 0)};
  const FeltDraws flush_draw =
      felt_draws(suited.data(), flop.data(), 3U);
  require(flush_draw.valid && flush_draw.improving_next_cards >= 9,
          "the flush draw was not counted");

  FeltGameState third = price_state(FELT_STREET_FLOP, 400, 133, kAll);
  require(felt_draw_price_is_right(&third, &flush_draw),
          "a flush draw folded to a third-pot bet on the flop");

  FeltGameState huge = price_state(FELT_STREET_FLOP, 400, 1600, kAll);
  require(!felt_draw_price_is_right(&huge, &flush_draw),
          "a flush draw called a four-times-pot bet");

  FeltGameState turn = price_state(FELT_STREET_TURN, 400, 300, kAll);
  const std::vector<FeltCard> turn_board = {card(6, 2), card(4, 2),
                                            card(11, 0), card(2, 1)};
  const FeltDraws turn_draw =
      felt_draws(suited.data(), turn_board.data(), 4U);
  require(!felt_draw_price_is_right(&turn, &turn_draw),
          "a flush draw called too much on the turn, where it sees one card");
}

void set_board(FeltGameState& state,
               const std::vector<FeltCard>& board) {
  std::memset(state.board, FELT_INVALID_CARD, sizeof(state.board));
  for (std::size_t index = 0; index < board.size(); index++) {
    state.board[index] = board[index];
  }
  state.board_count = static_cast<std::uint8_t>(board.size());
}

/* The bot itself, through the same shared-library path the harness uses. */
void test_bluff_guard(felt::NativeBotRunner& bot) {
  auto state = price_state(FELT_STREET_RIVER, 1000, 0, kNoBet);
  const std::vector<FeltCard> board = {card(12,2), card(11,3), card(9,0), card(2,1), card(1,2)};
  state.hole[0] = card(6,0);
  state.hole[1] = card(4,1);
  for (std::size_t i = 0; i < 5; ++i) state.board[i] = board[i];
  state.board_count = 5;
  state.decision_random = 1;
  state.my_stack = state.opp_stack = 499;
  state.max_raise_to = 499;
  require(bot.act(state).type == FELT_ACTION_CHECK, "slp-odds bluffed below half SPR");
  state.pot = 2000;
  state.to_call = state.opp_street_contribution = 1000;
  state.my_stack = 1000;
  state.opp_stack = 0;
  state.max_raise_to = 0;
  state.legal_actions = FELT_LEGAL_FOLD | FELT_LEGAL_CALL;
  require(bot.act(state).type == FELT_ACTION_FOLD, "slp-odds called an impossible bluff");
}

void test_policy(felt::NativeBotRunner& bot) {
  {
    /* Top pair, four to a flush, facing a pot-sized bet: too thin now. */
    FeltGameState state = price_state(FELT_STREET_TURN, 400, 400, kAll);
    state.hole[0] = card(11, 0);
    state.hole[1] = card(10, 1);
    set_board(state, {card(11, 2), card(5, 2), card(2, 2), card(7, 2)});
    const FeltAction action = bot.act(state);
    require(action.type == FELT_ACTION_FOLD,
            "paid off a pot-sized bet with top pair on a four-flush board");
  }
  {
    /* The same hand on a rainbow board is a call. */
    FeltGameState state = price_state(FELT_STREET_TURN, 400, 400, kAll);
    state.hole[0] = card(11, 0);
    state.hole[1] = card(10, 1);
    set_board(state, {card(11, 2), card(5, 1), card(2, 3), card(7, 0)});
    const FeltAction action = bot.act(state);
    require(action.type == FELT_ACTION_CALL,
            "folded top pair on a dry board to one bet");
  }
  {
    /* A set raises. */
    FeltGameState state = price_state(FELT_STREET_FLOP, 400, 200, kAll);
    state.hole[0] = card(5, 0);
    state.hole[1] = card(5, 1);
    set_board(state, {card(5, 2), card(11, 1), card(2, 3)});
    const FeltAction action = bot.act(state);
    require(action.type == FELT_ACTION_RAISE_TO, "a set only called");
  }
  {
    /* A bare flush draw calls a small bet and folds a huge one. */
    FeltGameState small = price_state(FELT_STREET_FLOP, 400, 133, kAll);
    small.hole[0] = card(12, 2);
    small.hole[1] = card(9, 2);
    set_board(small, {card(6, 2), card(4, 2), card(11, 0)});
    require(bot.act(small).type == FELT_ACTION_CALL,
            "folded a flush draw getting the price");

    FeltGameState big = price_state(FELT_STREET_FLOP, 400, 1600, kAll);
    big.hole[0] = card(12, 2);
    big.hole[1] = card(9, 2);
    set_board(big, {card(6, 2), card(4, 2), card(11, 0)});
    require(bot.act(big).type == FELT_ACTION_FOLD,
            "called four times the pot with a flush draw");
  }
  {
    /* A gutshot is not a flush draw. */
    FeltGameState state = price_state(FELT_STREET_FLOP, 400, 200, kAll);
    state.hole[0] = card(10, 0);
    state.hole[1] = card(8, 1);
    set_board(state, {card(11, 2), card(7, 1), card(2, 3)});
    require(bot.act(state).type == FELT_ACTION_FOLD,
            "paid half the pot for a gutshot");
  }
}

/* A raise of our own bet asks the two value bands for more points. */
void test_facing_raise_bands() {
  /* A dry set is 80: nutted against a bet, only strong against a raise. */
  require(felt_band_for_points(80, false) == FELT_BAND_NUTTED,
          "a dry set was not nutted against a bet");
  require(felt_band_for_points(80, true) == FELT_BAND_STRONG,
          "a dry set stayed nutted against a raise");
  /* A straight clears the higher bar too. */
  require(felt_band_for_points(86, true) == FELT_BAND_NUTTED,
          "a straight was demoted by a raise");
  /* An overpair is 54: strong against a bet, priced against a raise. */
  require(felt_band_for_points(54, false) == FELT_BAND_STRONG,
          "an overpair was not strong against a bet");
  require(felt_band_for_points(54, true) == FELT_BAND_MEDIUM,
          "an overpair stayed strong against a raise");
  /* The bluff-catching bands do not move; the price decides them. */
  require(felt_band_for_points(30, false) == felt_band_for_points(30, true),
          "the medium band moved");
  require(felt_band_for_points(20, false) == felt_band_for_points(20, true),
          "the marginal band moved");
}

/* Frequencies come from decision_random, so they are exact, not sampled. */
void test_mixes(felt::NativeBotRunner& bot) {
  int flatted = 0;
  int bluff_raised = 0;
  int bluff_reraised = 0;
  int trapped_dry = 0;
  int trapped_wet = 0;
  const int kTrials = 3000;

  for (int trial = 0; trial < kTrials; trial++) {
    {
      /* A straight facing a raise of our own bet. */
      FeltGameState state = price_state(FELT_STREET_TURN, 2000, 900, kAll);
      state.my_street_contribution = 300;
      state.opp_street_contribution = 1200;
      state.min_raise_to = 2100;
      state.hole[0] = card(11, 0);
      state.hole[1] = card(10, 1);
      set_board(state, {card(9, 2), card(8, 1), card(7, 3), card(2, 0)});
      state.decision_random = static_cast<std::uint64_t>(trial) * 2654435761ULL;
      if (bot.act(state).type == FELT_ACTION_CALL) flatted++;
    }
    {
      /* Nothing at all, facing an opening bet. */
      FeltGameState state = price_state(FELT_STREET_TURN, 400, 133, kAll);
      state.hole[0] = card(11, 0);
      state.hole[1] = card(9, 1);
      set_board(state, {card(6, 2), card(4, 1), card(2, 3), card(0, 0)});
      state.decision_random = static_cast<std::uint64_t>(trial) * 2654435761ULL;
      if (bot.act(state).type == FELT_ACTION_RAISE_TO) bluff_raised++;
    }
    {
      /* The same nothing, but our own bet got raised. */
      FeltGameState state = price_state(FELT_STREET_TURN, 2000, 900, kAll);
      state.my_street_contribution = 300;
      state.opp_street_contribution = 1200;
      state.min_raise_to = 2100;
      state.hole[0] = card(11, 0);
      state.hole[1] = card(9, 1);
      set_board(state, {card(6, 2), card(4, 1), card(2, 3), card(0, 0)});
      state.decision_random = static_cast<std::uint64_t>(trial) * 2654435761ULL;
      if (bot.act(state).type == FELT_ACTION_RAISE_TO) bluff_reraised++;
    }
    {
      /* A set on a dry board traps 30%: 20% base plus the dry-board nudge. */
      FeltGameState state = price_state(FELT_STREET_FLOP, 400, 0, kNoBet);
      state.hole[0] = card(5, 0);
      state.hole[1] = card(5, 1);
      set_board(state, {card(5, 2), card(11, 1), card(2, 3)});
      state.decision_random = static_cast<std::uint64_t>(trial) * 2654435761ULL;
      if (bot.act(state).type == FELT_ACTION_CHECK) trapped_dry++;
    }
    {
      /* A made flush on a wet board traps 10%: 20% base less protection. */
      FeltGameState state = price_state(FELT_STREET_FLOP, 400, 0, kNoBet);
      state.hole[0] = card(12, 2);
      state.hole[1] = card(9, 2);
      set_board(state, {card(6, 2), card(4, 2), card(11, 2)});
      state.decision_random = static_cast<std::uint64_t>(trial) * 2654435761ULL;
      if (bot.act(state).type == FELT_ACTION_CHECK) trapped_wet++;
    }
  }

  const double flat_rate = 100.0 * flatted / kTrials;
  const double bluff_rate = 100.0 * bluff_raised / kTrials;
  if (flat_rate < 21.0 || flat_rate > 29.0) {
    throw std::runtime_error("nutted flatted a raise " +
                             std::to_string(flat_rate) + "% of the time, not a quarter");
  }
  if (bluff_rate < 15.0 || bluff_rate > 25.0) {
    throw std::runtime_error("air raised a bet " + std::to_string(bluff_rate) +
                             "% of the time, not a fifth");
  }
  const double reraise_rate = 100.0 * bluff_reraised / kTrials;
  if (reraise_rate < 15.0 || reraise_rate > 25.0) {
    throw std::runtime_error("air three-bet " + std::to_string(reraise_rate) +
                             "% of the time, not a fifth");
  }
  const double dry_rate = 100.0 * trapped_dry / kTrials;
  const double wet_rate = 100.0 * trapped_wet / kTrials;
  if (dry_rate < 26.0 || dry_rate > 34.0 || wet_rate < 6.0 ||
      wet_rate > 14.0 || dry_rate <= wet_rate) {
    throw std::runtime_error(
        "board texture did not move unopened trap rates: dry " +
        std::to_string(dry_rate) + "%, wet " + std::to_string(wet_rate) + "%");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "expected the slp_odds bot library path\n";
    return 2;
  }
  try {
    felt_bot_kit_warmup();
    test_board_relative_value();
    test_score_table_edge_cases();
    test_call_price();
    test_draw_price();
    test_facing_raise_bands();
    felt::NativeBotRunner bot(argv[1]);
    test_bluff_guard(bot);
    test_policy(bot);
    test_mixes(bot);
  } catch (const std::exception& error) {
    std::cerr << "board_value_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

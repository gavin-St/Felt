#include "felt/bot_api.h"
#include "felt/bot_kit.h"
#include "felt/native_bot_runner.hpp"

extern "C" {
#include "board_value.h"
}

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
  const FeltMadeHand made =
      felt_made_hand(hole.data(), board.data(),
                     static_cast<std::uint8_t>(board.size()));
  const FeltBoardTexture texture = felt_board_texture(
      board.data(), static_cast<std::uint8_t>(board.size()));
  return felt_board_relative_value(&made, &texture);
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

  /* And with four to a straight. */
  expect_band(king_queen, {card(11, 2), card(4, 1), card(3, 3), card(2, 0)},
              FELT_BAND_MEDIUM, "top pair into four to a straight");

  /* A set is nutted on a dry board and merely strong on a wet one. */
  const std::array<FeltCard, 2> sevens = {card(5, 0), card(5, 1)};
  expect_band(sevens, {card(5, 2), card(11, 1), card(2, 3)},
              FELT_BAND_NUTTED, "a set on a dry board");
  expect_band(sevens, {card(5, 2), card(11, 2), card(2, 2), card(9, 2)},
              FELT_BAND_STRONG, "a set into four to a flush");

  /* Weak pairs and nothing. */
  expect_band(king_queen, {card(12, 3), card(10, 2), card(2, 1)},
              FELT_BAND_MARGINAL, "middle pair");
  expect_band(king_queen, {card(8, 3), card(6, 2), card(2, 1)},
              FELT_BAND_AIR, "king high");

  /* A made flush is not frightened by the three suited cards making it. */
  const std::array<FeltCard, 2> suited = {card(12, 2), card(9, 2)};
  expect_band(suited, {card(6, 2), card(4, 2), card(2, 2)},
              FELT_BAND_NUTTED, "a made flush");
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

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "expected the slp_odds bot library path\n";
    return 2;
  }
  try {
    felt_bot_kit_warmup();
    test_board_relative_value();
    test_call_price();
    test_draw_price();
    felt::NativeBotRunner bot(argv[1]);
    test_policy(bot);
  } catch (const std::exception& error) {
    std::cerr << "board_value_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

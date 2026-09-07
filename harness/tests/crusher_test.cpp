#include "felt/bot_api.h"
#include "felt/bot_kit.h"
#include "felt/native_bot_runner.hpp"

#include "board_value.h"
#include "range_read.h"

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

  FeltGameState state(std::uint32_t street, FeltChips pot, FeltChips to_call,
                      std::uint32_t legal) const {
    FeltGameState s{};
    s.abi_version = FELT_BOT_ABI_VERSION;
    s.struct_size = sizeof(FeltGameState);
    s.street = street;
    s.position = FELT_POSITION_BIG_BLIND;
    s.pot = pot;
    s.to_call = to_call;
    s.my_stack = 20000 - pot / 2;
    s.opp_stack = 20000 - pot / 2;
    s.opp_street_contribution = to_call;
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

/* A line that has shown nothing scores low; one that has raised twice does
 * not. This is the whole opponent model. */
void test_range_score() {
  const std::uint32_t them = FELT_POSITION_BUTTON;

  Hand checked;
  checked.blinds();
  checked.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  checked.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  checked.add(them, FELT_STREET_FLOP, FELT_EVENT_CHECK, 0);
  const FeltGameState quiet_state = checked.state(FELT_STREET_FLOP, 500, 0, kNoBet);
  const FeltRangeRead quiet = felt_read_range(&quiet_state);

  Hand bet = checked;
  bet.history.pop_back();
  bet.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 330);
  const FeltGameState bet_state = bet.state(FELT_STREET_FLOP, 830, 330, kAll);
  const FeltRangeRead betting = felt_read_range(&bet_state);

  Hand raised = checked;
  raised.history.pop_back();
  raised.add(FELT_POSITION_BIG_BLIND, FELT_STREET_FLOP, FELT_EVENT_BET, 330);
  raised.add(them, FELT_STREET_FLOP, FELT_EVENT_RAISE, 1200);
  FeltGameState raised_state =
      raised.state(FELT_STREET_FLOP, 1860, 870, kAll);
  raised_state.my_street_contribution = 330;
  const FeltRangeRead raising = felt_read_range(&raised_state);

  require(quiet.valid && betting.valid && raising.valid, "range read failed");
  require(quiet.score < betting.score,
          "a checking range did not score below a betting one");
  require(betting.score < raising.score,
          "a betting range did not score below a raising one");
  require(quiet.score < 35 && raising.score > 60,
          "the range scale is not spread out: " + std::to_string(quiet.score) +
              " to " + std::to_string(raising.score));
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
  if (weak < 25.0) {
    throw std::runtime_error("bluffed a range that showed nothing only " +
                             std::to_string(weak) + "% of the time");
  }
  if (tough > 18.0) {
    throw std::runtime_error("bluffed a range that raised twice " +
                             std::to_string(tough) + "% of the time");
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
    test_geometric_sizing();
    felt::NativeBotRunner bot(argv[1]);
    test_same_hand_two_ranges(bot);
    test_bluff_frequency_tracks_the_range(bot);
  } catch (const std::exception& error) {
    std::cerr << "crusher_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

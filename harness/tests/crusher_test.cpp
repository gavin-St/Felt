#include "felt/bot_api.h"
#include "felt/bot_kit.h"
#include "felt/native_bot_runner.hpp"

#include "board_value.h"
#include "bet_sizing.h"
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

/* Each preflop raise narrows a range, and not by the same amount each time. */
void test_preflop_ladder() {
  const std::uint32_t them = FELT_POSITION_BUTTON;
  const std::vector<FeltCard> board = {card(11, 2), card(9, 1), card(2, 3)};
  const FeltBoardTexture texture = felt_board_texture(board.data(), 3U);

  Hand limped;
  limped.blinds();
  limped.add(them, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 100);
  limped.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CHECK, 100);
  limped.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 130);
  const FeltGameState limped_state =
      limped.state(FELT_STREET_FLOP, 330, 130, kAll);

  Hand opened;
  opened.blinds();
  opened.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  opened.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  opened.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 330);
  const FeltGameState opened_state =
      opened.state(FELT_STREET_FLOP, 830, 330, kAll);

  Hand three_bet;
  three_bet.blinds();
  three_bet.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  three_bet.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 900);
  three_bet.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 900);
  three_bet.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 1200);
  const FeltGameState three_bet_state =
      three_bet.state(FELT_STREET_FLOP, 3000, 1200, kAll);

  Hand four_bet = three_bet;
  four_bet.history.pop_back();
  four_bet.history.pop_back();
  four_bet.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 2600);
  four_bet.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 7000);
  four_bet.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 7000);
  four_bet.add(them, FELT_STREET_FLOP, FELT_EVENT_BET, 9000);
  const FeltGameState four_bet_state =
      four_bet.state(FELT_STREET_FLOP, 23000, 9000, kAll);

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
  const FeltDraws none{};

  Hand opened;
  opened.blinds();
  opened.add(them, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  opened.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  opened.add(them, FELT_STREET_FLOP, FELT_EVENT_CHECK, 0);
  const FeltGameState state = opened.state(FELT_STREET_FLOP, 500, 0, kNoBet);
  FeltRangeRead read = felt_read_range(&state, &texture);

  read.polarisation = 80;
  const FeltSizing polarised_value =
      felt_choose_size(&state, &read, &texture, &none, FELT_SIZING_VALUE,
                       false, 0);
  require(polarised_value.large > 1.2 && polarised_value.small < 0.6,
          "a value bet into a polarised range did not get the big pair");

  read.polarisation = 20;
  const FeltSizing merged_value =
      felt_choose_size(&state, &read, &texture, &none, FELT_SIZING_VALUE,
                       false, 0);
  require(merged_value.large < 0.7,
          "a value bet into a merged range kept the big pair");

  const FeltSizing thin =
      felt_choose_size(&state, &read, &texture, &none, FELT_SIZING_THIN_VALUE,
                       false, 0);
  require(thin.small > 0.3 && thin.small < 0.4 && thin.large > 0.45 &&
              thin.large < 0.55,
          "thin value was not a third and a half");

  const FeltSizing reraise =
      felt_choose_size(&state, &read, &texture, &none, FELT_SIZING_VALUE, true,
                       0);
  require(reraise.small >= 2.0,
          "a re-raise was sized like an opening bet");

  /* The geometric plan pulls the weight toward whichever size is nearer. */
  read.polarisation = 20;
  const FeltSizing near_small =
      felt_choose_size(&state, &read, &texture, &none, FELT_SIZING_VALUE,
                       false, 30);
  const FeltSizing near_large =
      felt_choose_size(&state, &read, &texture, &none, FELT_SIZING_VALUE,
                       false, 70);
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
    test_range_advantage();
    test_preflop_ladder();
    test_polarisation();
    test_sizing_pairs();
    test_sizes_overlap();
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

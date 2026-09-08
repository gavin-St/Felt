#include "felt/bot_api.h"
#include "felt/native_bot_runner.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr FeltCard card(std::uint8_t rank, std::uint8_t suit) {
  return static_cast<FeltCard>(rank * 4U + suit);
}

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

constexpr std::uint32_t kAll =
    FELT_LEGAL_FOLD | FELT_LEGAL_CALL | FELT_LEGAL_RAISE_TO;
constexpr std::uint32_t kNoBet = FELT_LEGAL_CHECK | FELT_LEGAL_RAISE_TO;

struct Builder {
  std::vector<FeltActionEvent> history;

  void post_blinds() {
    add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP,
        FELT_EVENT_POST_SMALL_BLIND, 50);
    add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP,
        FELT_EVENT_POST_BIG_BLIND, 100);
  }

  void add(std::uint32_t position,
           std::uint32_t street,
           std::uint32_t type,
           FeltChips amount_to) {
    FeltActionEvent event{};
    event.position = position;
    event.street = street;
    event.type = type;
    event.amount_to = amount_to;
    history.push_back(event);
  }

  FeltGameState state(std::uint32_t street,
                      std::uint32_t position,
                      FeltCard first,
                      FeltCard second,
                      FeltChips pot,
                      FeltChips to_call,
                      std::uint32_t legal) const {
    FeltGameState state{};
    state.abi_version = FELT_BOT_ABI_VERSION;
    state.struct_size = sizeof(FeltGameState);
    state.hole[0] = first;
    state.hole[1] = second;
    std::memset(state.board, FELT_INVALID_CARD, sizeof(state.board));
    state.street = street;
    state.position = position;
    state.pot = pot;
    state.to_call = to_call;
    state.my_stack = 20000 - pot / 2;
    state.opp_stack = 20000 - pot / 2;
    /* Preflop the blinds are already posted, so the acting player's own
     * contribution is its blind and the opponent's is that plus the call. */
    state.my_street_contribution =
        street == FELT_STREET_PREFLOP
            ? (position == FELT_POSITION_BUTTON ? 50 : 100)
            : 0;
    state.opp_street_contribution = state.my_street_contribution + to_call;
    const FeltChips last_raise =
        std::max<FeltChips>(state.opp_street_contribution -
                                state.my_street_contribution,
                            100);
    state.min_raise_to = state.opp_street_contribution + last_raise;
    state.max_raise_to = 20000;
    state.legal_actions = legal;
    state.history = history.data();
    state.history_count = static_cast<std::uint32_t>(history.size());
    return state;
  }
};

void set_board(FeltGameState& state,
               std::array<FeltCard, 5> board,
               std::uint8_t count) {
  for (std::uint8_t index = 0; index < 5U; index++) {
    state.board[index] = index < count ? board[index] : FELT_INVALID_CARD;
  }
  state.board_count = count;
}

void expect(felt::NativeBotRunner& bot,
            const FeltGameState& state,
            std::uint32_t action,
            const std::string& context) {
  const FeltAction result = bot.act(state);
  require(result.type == action, context);
}

/* Nancy opens only TT+/AT+/KQ, continues to a 3-bet only with QQ+/AK, and
 * never bluffs after the flop. */
void test_nitty_nancy(felt::NativeBotRunner& bot) {
  Builder open;
  open.post_blinds();
  {
    const FeltGameState state = open.state(FELT_STREET_PREFLOP,
                                           FELT_POSITION_BUTTON,
                                           card(5, 0), card(0, 1),
                                           150, 50, kAll);
    expect(bot, state, FELT_ACTION_FOLD, "nancy played 72o");
  }
  {
    const FeltGameState state = open.state(FELT_STREET_PREFLOP,
                                           FELT_POSITION_BUTTON,
                                           card(12, 0), card(12, 1),
                                           150, 50, kAll);
    expect(bot, state, FELT_ACTION_RAISE_TO, "nancy did not raise aces");
  }

  Builder three_bet;
  three_bet.post_blinds();
  three_bet.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  three_bet.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 900);
  {
    const FeltGameState state = three_bet.state(FELT_STREET_PREFLOP,
                                                FELT_POSITION_BUTTON,
                                                card(8, 0), card(8, 1),
                                                1150, 650, kAll);
    expect(bot, state, FELT_ACTION_FOLD, "nancy called a 3-bet with tens");
  }
  {
    const FeltGameState state = three_bet.state(FELT_STREET_PREFLOP,
                                                FELT_POSITION_BUTTON,
                                                card(12, 0), card(11, 1),
                                                1150, 650, kAll);
    expect(bot, state, FELT_ACTION_RAISE_TO, "nancy folded ace-king to a 3-bet");
  }
  {
    FeltGameState state = open.state(FELT_STREET_FLOP, FELT_POSITION_BUTTON,
                                     card(5, 0), card(0, 1), 400, 0, kNoBet);
    set_board(state, {card(12, 2), card(9, 3), card(2, 0), 0, 0}, 3U);
    expect(bot, state, FELT_ACTION_CHECK, "nancy bluffed");
  }
}

/* The station calls any preflop size inside the opening chart, and calls a
 * postflop bet with any pair or any draw, at any size. */
void test_calling_station(felt::NativeBotRunner& bot) {
  Builder shove;
  shove.post_blinds();
  shove.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 8000);
  {
    const FeltGameState state = shove.state(FELT_STREET_PREFLOP,
                                            FELT_POSITION_BIG_BLIND,
                                            card(12, 0), card(11, 1),
                                            8100, 7900, kAll);
    expect(bot, state, FELT_ACTION_CALL, "station folded ace-king to 80bb");
  }
  {
    FeltGameState state = shove.state(FELT_STREET_FLOP, FELT_POSITION_BIG_BLIND,
                                      card(9, 0), card(3, 1), 800, 400, kAll);
    set_board(state, {card(9, 2), card(5, 3), card(2, 0), 0, 0}, 3U);
    expect(bot, state, FELT_ACTION_CALL, "station folded a pair");
  }
  {
    /* King-ten of hearts on a two-heart flop: a bare flush draw, facing a
     * raise of the station's own bet. The shared policy folds most draws to a
     * raise, so the station has to carry this one itself. */
    FeltGameState state = shove.state(FELT_STREET_FLOP, FELT_POSITION_BIG_BLIND,
                                      card(11, 3), card(8, 3), 3600, 900, kAll);
    set_board(state, {card(5, 3), card(2, 3), card(0, 0), 0, 0}, 3U);
    state.my_street_contribution = 300;
    state.opp_street_contribution = 1200;
    state.min_raise_to = 2100;
    expect(bot, state, FELT_ACTION_CALL, "station folded a draw to a raise");
  }
  {
    /* A pair that exists entirely on the board is not one of the station's
     * player-made pairs, so its own "call any pair at any price" rule does
     * not fire and the shared policy decides. That policy now treats the
     * board's pair as showdown value, which calls a bet but gives up to a
     * raise -- and a raise is where the difference shows. */
    FeltGameState state = shove.state(FELT_STREET_FLOP,
                                      FELT_POSITION_BIG_BLIND,
                                      card(6, 0), card(2, 1), 3600, 900, kAll);
    set_board(state, {card(11, 2), card(11, 3), card(0, 0), 0, 0}, 3U);
    state.my_street_contribution = 300;
    state.opp_street_contribution = 1200;
    state.min_raise_to = 2100;
    expect(bot, state, FELT_ACTION_FOLD,
           "station treated a board-only pair as player-made");
  }
  {
    /* Against an opening bet the same hand is a call, from the shared
     * showdown line rather than from the station's own rule. */
    FeltGameState state = shove.state(FELT_STREET_FLOP,
                                      FELT_POSITION_BIG_BLIND,
                                      card(6, 0), card(2, 1), 800, 400, kAll);
    set_board(state, {card(11, 2), card(11, 3), card(0, 0), 0, 0}, 3U);
    expect(bot, state, FELT_ACTION_CALL,
           "the board's pair was folded to an opening bet");
  }
}

/* Patty turns every raise into a call and folds anything below top pair. */
void test_passive_patty(felt::NativeBotRunner& bot) {
  Builder builder;
  builder.post_blinds();
  {
    const FeltGameState state = builder.state(FELT_STREET_PREFLOP,
                                              FELT_POSITION_BUTTON,
                                              card(10, 0), card(10, 1),
                                              150, 50, kAll);
    expect(bot, state, FELT_ACTION_CALL, "patty raised preflop");
  }
  {
    FeltGameState state = builder.state(FELT_STREET_FLOP, FELT_POSITION_BUTTON,
                                        card(4, 0), card(1, 1), 400, 200, kAll);
    set_board(state, {card(12, 2), card(9, 3), card(2, 0), 0, 0}, 3U);
    expect(bot, state, FELT_ACTION_FOLD, "patty continued below top pair");
  }
  {
    /* A flush our own two hearts made: the board shows three. */
    FeltGameState state = builder.state(FELT_STREET_RIVER, FELT_POSITION_BUTTON,
                                        card(12, 3), card(9, 3), 400, 0, kNoBet);
    set_board(state, {card(10, 3), card(7, 3), card(2, 3), card(5, 0),
                      card(1, 1)}, 5U);
    expect(bot, state, FELT_ACTION_RAISE_TO, "patty checked her own flush");
  }
  {
    /* Four hearts on the board and everybody has one: back to checking. */
    FeltGameState state = builder.state(FELT_STREET_RIVER, FELT_POSITION_BUTTON,
                                        card(12, 3), card(4, 0), 400, 0, kNoBet);
    set_board(state, {card(10, 3), card(7, 3), card(2, 3), card(5, 3),
                      card(1, 1)}, 5U);
    expect(bot, state, FELT_ACTION_CHECK, "patty bet a board flush");
  }
  {
    /* A straight the board is only three to. */
    FeltGameState state = builder.state(FELT_STREET_RIVER, FELT_POSITION_BUTTON,
                                        card(6, 0), card(5, 1), 400, 0, kNoBet);
    set_board(state, {card(4, 2), card(3, 3), card(2, 0), card(11, 1),
                      card(10, 2)}, 5U);
    expect(bot, state, FELT_ACTION_RAISE_TO, "patty checked her own straight");
  }
  {
    /* Four to the straight on the board: shared, so she checks it. */
    FeltGameState state = builder.state(FELT_STREET_RIVER, FELT_POSITION_BUTTON,
                                        card(6, 0), card(11, 1), 400, 0, kNoBet);
    set_board(state, {card(5, 2), card(4, 3), card(3, 0), card(2, 1),
                      card(0, 2)}, 5U);
    expect(bot, state, FELT_ACTION_CHECK, "patty bet a board straight");
  }
  {
    /* Any full house is hers to bet. */
    FeltGameState state = builder.state(FELT_STREET_RIVER, FELT_POSITION_BUTTON,
                                        card(9, 0), card(9, 1), 400, 0, kNoBet);
    set_board(state, {card(9, 2), card(4, 3), card(4, 0), card(11, 1),
                      card(0, 2)}, 5U);
    expect(bot, state, FELT_ACTION_RAISE_TO, "patty checked a full house");
  }
}

/* Charlie calls a draw at any price. */
void test_chasing_charlie(felt::NativeBotRunner& bot) {
  Builder builder;
  builder.post_blinds();
  FeltGameState state = builder.state(FELT_STREET_FLOP, FELT_POSITION_BIG_BLIND,
                                      card(6, 3), card(3, 3), 400, 3000, kAll);
  set_board(state, {card(12, 3), card(9, 3), card(2, 0), 0, 0}, 3U);
  expect(bot, state, FELT_ACTION_CALL, "charlie folded a flush draw");

  /* Preflop the suited call stops at 16 bb, the top of the three-bet band. */
  Builder raised;
  raised.post_blinds();
  raised.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 1600);
  {
    FeltGameState under = raised.state(FELT_STREET_PREFLOP,
                                       FELT_POSITION_BIG_BLIND,
                                       card(6, 3), card(3, 3),
                                       1700, 1500, kAll);
    expect(bot, under, FELT_ACTION_CALL, "charlie folded a suited hand at 15bb");
  }
  {
    FeltGameState over = raised.state(FELT_STREET_PREFLOP,
                                      FELT_POSITION_BIG_BLIND,
                                      card(6, 3), card(3, 3),
                                      1800, 1600, kAll);
    expect(bot, over, FELT_ACTION_FOLD, "charlie called 16bb with 64s");
  }
}

/* Sarah raises draws and folds every other unpaired hand. */
void test_semi_bluff_sarah(felt::NativeBotRunner& bot) {
  Builder builder;
  builder.post_blinds();
  {
    FeltGameState state = builder.state(FELT_STREET_FLOP,
                                        FELT_POSITION_BIG_BLIND,
                                        card(6, 3), card(3, 3), 400, 200, kAll);
    set_board(state, {card(12, 3), card(9, 3), card(2, 0), 0, 0}, 3U);
    expect(bot, state, FELT_ACTION_RAISE_TO, "sarah did not raise a draw");
  }
  {
    FeltGameState state = builder.state(FELT_STREET_FLOP,
                                        FELT_POSITION_BIG_BLIND,
                                        card(6, 0), card(3, 1), 400, 200, kAll);
    set_board(state, {card(12, 2), card(9, 3), card(2, 0), 0, 0}, 3U);
    expect(bot, state, FELT_ACTION_FOLD, "sarah continued with air");
  }
}

/* Thomas traps premiums preflop, slow-plays flop and turn, and releases the
 * aggression only on the river. */
void test_trapping_thomas(felt::NativeBotRunner& bot) {
  Builder builder;
  builder.post_blinds();
  {
    const FeltGameState state = builder.state(FELT_STREET_PREFLOP,
                                              FELT_POSITION_BUTTON,
                                              card(12, 0), card(12, 1),
                                              150, 50, kAll);
    expect(bot, state, FELT_ACTION_CALL, "thomas raised aces preflop");
  }

  const std::array<FeltCard, 5> board = {
      card(12, 2), card(9, 3), card(2, 0), card(6, 1), card(1, 2)};
  {
    FeltGameState state = builder.state(FELT_STREET_FLOP,
                                        FELT_POSITION_BIG_BLIND,
                                        card(12, 0), card(3, 1), 400, 0, kNoBet);
    set_board(state, board, 3U);
    expect(bot, state, FELT_ACTION_CHECK, "thomas opened with top pair");
  }
  {
    FeltGameState state = builder.state(FELT_STREET_FLOP,
                                        FELT_POSITION_BIG_BLIND,
                                        card(12, 0), card(3, 1), 400, 200, kAll);
    set_board(state, board, 3U);
    expect(bot, state, FELT_ACTION_CALL, "thomas raised the flop instead of trapping");
  }
  {
    FeltGameState state = builder.state(FELT_STREET_TURN,
                                        FELT_POSITION_BUTTON,
                                        card(12, 0), card(3, 1), 800, 0, kNoBet);
    set_board(state, board, 4U);
    expect(bot, state, FELT_ACTION_CHECK, "thomas bet a good hand on the turn");
  }
  {
    FeltGameState state = builder.state(FELT_STREET_RIVER,
                                        FELT_POSITION_BIG_BLIND,
                                        card(12, 0), card(3, 1), 1200, 300, kAll);
    set_board(state, board, 5U);
    expect(bot, state, FELT_ACTION_RAISE_TO,
           "thomas did not check-raise the river out of position");
  }
  {
    FeltGameState state = builder.state(FELT_STREET_RIVER,
                                        FELT_POSITION_BUTTON,
                                        card(12, 0), card(3, 1), 1200, 0, kNoBet);
    set_board(state, board, 5U);
    expect(bot, state, FELT_ACTION_RAISE_TO,
           "thomas checked back the river in position");
  }
}

/* Travis barrels every street; one-and-done gives up after the flop. */
void test_barrel_policies(felt::NativeBotRunner& travis,
                          felt::NativeBotRunner& one_and_done) {
  Builder builder;
  builder.post_blinds();
  builder.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  builder.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  const std::array<FeltCard, 5> runout = {card(12, 2), card(9, 3), card(2, 0),
                                          card(7, 1), card(4, 2)};
  {
    FeltGameState state = builder.state(FELT_STREET_RIVER, FELT_POSITION_BUTTON,
                                        card(6, 0), card(3, 1), 500, 0, kNoBet);
    set_board(state, runout, 5U);
    expect(travis, state, FELT_ACTION_RAISE_TO, "travis stopped barrelling");
    expect(one_and_done, state, FELT_ACTION_CHECK, "one-and-done kept barrelling");
  }
  {
    FeltGameState state = builder.state(FELT_STREET_FLOP, FELT_POSITION_BUTTON,
                                        card(6, 0), card(3, 1), 500, 0, kNoBet);
    set_board(state, runout, 3U);
    expect(one_and_done, state, FELT_ACTION_RAISE_TO, "one-and-done skipped its c-bet");
  }
}

/* Sam has exact 25 bb and 50 bb fear thresholds. */
void test_scared_sam(felt::NativeBotRunner& bot) {
  Builder builder;
  builder.post_blinds();
  {
    /* Facing 24 bb, the chart's jam with queens still goes in. */
    Builder raised;
    raised.post_blinds();
    raised.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP,
               FELT_EVENT_RAISE, 2500);
    FeltGameState state = raised.state(FELT_STREET_PREFLOP,
                                       FELT_POSITION_BIG_BLIND,
                                       card(10, 3), card(10, 1),
                                       2600, 2400, kAll);
    expect(bot, state, FELT_ACTION_RAISE_TO,
           "sam flinched below the 25bb preflop tier");
  }
  {
    /* One big blind more and the same hand only calls. */
    Builder raised;
    raised.post_blinds();
    raised.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP,
               FELT_EVENT_RAISE, 2600);
    FeltGameState state = raised.state(FELT_STREET_PREFLOP,
                                       FELT_POSITION_BIG_BLIND,
                                       card(10, 3), card(10, 1),
                                       2700, 2500, kAll);
    expect(bot, state, FELT_ACTION_CALL,
           "sam raised with 25bb in front of him");
  }
  {
    FeltGameState state = builder.state(FELT_STREET_FLOP,
                                        FELT_POSITION_BIG_BLIND,
                                        card(12, 0), card(3, 1),
                                        5000, 2500, kAll);
    set_board(state, {card(12, 2), card(9, 3), card(2, 0), 0, 0}, 3U);
    expect(bot, state, FELT_ACTION_FOLD,
           "sam called 25bb with only top pair");
  }
  {
    FeltGameState state = builder.state(FELT_STREET_RIVER,
                                        FELT_POSITION_BIG_BLIND,
                                        card(12, 0), card(11, 1),
                                        5000, 2500, kAll);
    set_board(state, {card(12, 2), card(11, 3), card(4, 0), card(2, 1),
                      card(0, 2)}, 5U);
    expect(bot, state, FELT_ACTION_CALL,
           "sam folded strong two pair at the 25bb tier");
  }
  {
    FeltGameState state = builder.state(FELT_STREET_FLOP,
                                        FELT_POSITION_BIG_BLIND,
                                        card(12, 0), card(12, 1),
                                        8000, 5000, kAll);
    set_board(state, {card(12, 2), card(9, 3), card(2, 0), 0, 0}, 3U);
    expect(bot, state, FELT_ACTION_FOLD,
           "sam called 50bb with a set instead of requiring a straight");
  }
  {
    FeltGameState state = builder.state(FELT_STREET_RIVER,
                                        FELT_POSITION_BIG_BLIND,
                                        card(8, 0), card(7, 1),
                                        8000, 5000, kAll);
    set_board(state, {card(6, 2), card(5, 3), card(4, 0), card(1, 1),
                      card(0, 2)}, 5U);
    expect(bot, state, FELT_ACTION_CALL,
           "sam folded a straight at the 50bb tier");
  }
  {
    FeltGameState state = builder.state(FELT_STREET_FLOP,
                                        FELT_POSITION_BIG_BLIND,
                                        card(12, 0), card(12, 1),
                                        5000, 0, kNoBet);
    set_board(state, {card(12, 2), card(9, 3), card(2, 0), 0, 0}, 3U);
    state.decision_random = 1;
    expect(bot, state, FELT_ACTION_CHECK,
           "sam raised after the postflop pot reached 50bb");
  }
}

/* Terry bluffs every air hand but folds it once re-raised. */
void test_tilted_terry(felt::NativeBotRunner& bot) {
  const std::array<FeltCard, 5> board = {card(12, 2), card(9, 3), card(2, 0), 0, 0};
  {
    Builder builder;
    builder.post_blinds();
    FeltGameState state = builder.state(FELT_STREET_FLOP,
                                        FELT_POSITION_BIG_BLIND,
                                        card(6, 0), card(3, 1), 400, 0, kNoBet);
    set_board(state, board, 3U);
    expect(bot, state, FELT_ACTION_RAISE_TO, "terry did not bluff");
  }
  {
    Builder builder;
    builder.post_blinds();
    builder.add(FELT_POSITION_BIG_BLIND, FELT_STREET_FLOP, FELT_EVENT_BET, 200);
    builder.add(FELT_POSITION_BUTTON, FELT_STREET_FLOP, FELT_EVENT_RAISE, 800);
    FeltGameState state = builder.state(FELT_STREET_FLOP,
                                        FELT_POSITION_BIG_BLIND,
                                        card(6, 0), card(3, 1), 1400, 600, kAll);
    set_board(state, board, 3U);
    expect(bot, state, FELT_ACTION_FOLD, "terry kept bluffing into a re-raise");
  }
  {
    /* Any two cards call an open, up to 50 bb. */
    Builder builder;
    builder.post_blinds();
    builder.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP,
                FELT_EVENT_RAISE, 5000);
    FeltGameState state = builder.state(FELT_STREET_PREFLOP,
                                        FELT_POSITION_BIG_BLIND,
                                        card(5, 0), card(0, 1),
                                        5100, 4900, kAll);
    expect(bot, state, FELT_ACTION_CALL, "terry folded 72o to a 49bb open");
  }
  {
    /* One big blind more is all-in sized, and 72o goes back on the chart. */
    Builder builder;
    builder.post_blinds();
    builder.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP,
                FELT_EVENT_RAISE, 5100);
    FeltGameState state = builder.state(FELT_STREET_PREFLOP,
                                        FELT_POSITION_BIG_BLIND,
                                        card(5, 0), card(0, 1),
                                        5200, 5000, kAll);
    expect(bot, state, FELT_ACTION_FOLD, "terry called 50bb with 72o");
  }
}

/* Chalamet never opens the betting: everything he would have bet waits for
 * the opponent to bet and comes back as a raise. */
void test_check_raise_chalamet(felt::NativeBotRunner& bot) {
  const std::array<FeltCard, 5> board = {card(12, 2), card(9, 3), card(2, 0),
                                         0, 0};
  Builder builder;
  builder.post_blinds();
  {
    /* Small blind, nothing in front of him: limp, never open. */
    FeltGameState state = builder.state(FELT_STREET_PREFLOP,
                                        FELT_POSITION_BUTTON,
                                        card(12, 3), card(12, 1),
                                        150, 50, kAll);
    expect(bot, state, FELT_ACTION_CALL, "chalamet opened with aces");
  }
  {
    /* He limps the chart's range, not every hand: 72o is still a fold. */
    FeltGameState state = builder.state(FELT_STREET_PREFLOP,
                                        FELT_POSITION_BUTTON,
                                        card(5, 0), card(0, 1),
                                        150, 50, kAll);
    expect(bot, state, FELT_ACTION_FOLD, "chalamet limped 72o");
  }
  {
    /* Top pair in an unbet pot checks rather than betting. */
    FeltGameState state = builder.state(FELT_STREET_FLOP,
                                        FELT_POSITION_BIG_BLIND,
                                        card(12, 0), card(3, 1), 400, 0,
                                        kNoBet);
    set_board(state, board, 3U);
    expect(bot, state, FELT_ACTION_CHECK, "chalamet led out with top pair");
  }
  {
    /* The same hand, once bet into, is a raise. */
    FeltGameState state = builder.state(FELT_STREET_FLOP,
                                        FELT_POSITION_BIG_BLIND,
                                        card(12, 0), card(3, 1), 600, 200,
                                        kAll);
    set_board(state, board, 3U);
    expect(bot, state, FELT_ACTION_RAISE_TO,
           "chalamet flat-called with top pair");
  }
  {
    /* A draw is the bluff half of the raising range. */
    FeltGameState state = builder.state(FELT_STREET_TURN,
                                        FELT_POSITION_BIG_BLIND,
                                        card(6, 3), card(3, 3), 600, 200,
                                        kAll);
    set_board(state, {card(12, 3), card(9, 3), card(2, 0), card(7, 1), 0}, 4U);
    expect(bot, state, FELT_ACTION_RAISE_TO, "chalamet did not raise a draw");
  }
  {
    /* There is no calling range: pure air check-raises too. */
    FeltGameState state = builder.state(FELT_STREET_RIVER,
                                        FELT_POSITION_BIG_BLIND,
                                        card(6, 0), card(3, 1), 600, 200,
                                        kAll);
    set_board(state, {card(12, 2), card(9, 3), card(2, 0), card(7, 1),
                      card(4, 2)}, 5U);
    expect(bot, state, FELT_ACTION_RAISE_TO,
           "chalamet found a fold before being raised");
  }
  {
    /* Once the check-raise is raised he is on the shared policy, and air
     * there is a fold rather than another raise. */
    FeltGameState state = builder.state(FELT_STREET_RIVER,
                                        FELT_POSITION_BIG_BLIND,
                                        card(6, 0), card(3, 1), 2400, 1200,
                                        kAll);
    set_board(state, {card(12, 2), card(9, 3), card(2, 0), card(7, 1),
                      card(4, 2)}, 5U);
    state.my_street_contribution = 600;
    state.opp_street_contribution = 1800;
    expect(bot, state, FELT_ACTION_FOLD, "chalamet kept raising after a raise");
  }
}

/* A third of Thomas's turn value hands spring the trap early. */
void test_trapping_thomas_turn(felt::NativeBotRunner& bot) {
  Builder builder;
  builder.post_blinds();
  builder.add(FELT_POSITION_BUTTON, FELT_STREET_TURN, FELT_EVENT_BET, 200);
  FeltGameState state = builder.state(FELT_STREET_TURN,
                                      FELT_POSITION_BIG_BLIND,
                                      card(12, 0), card(3, 1), 600, 200, kAll);
  set_board(state, {card(12, 2), card(9, 3), card(2, 0), card(7, 1), 0}, 4U);
  state.decision_random = UINT64_C(0);
  expect(bot, state, FELT_ACTION_RAISE_TO,
         "thomas never check-raises the turn");
  state.decision_random = UINT64_C(1) << 24U;
  expect(bot, state, FELT_ACTION_CALL,
         "thomas check-raised every turn value hand");
}

/* The two sizing overrides keep the same decision and change only the amount. */
void test_sizing_overrides(felt::NativeBotRunner& miranda,
                           felt::NativeBotRunner& oliver) {
  Builder builder;
  builder.post_blinds();
  const FeltGameState state = builder.state(FELT_STREET_PREFLOP,
                                            FELT_POSITION_BUTTON,
                                            card(12, 1), card(10, 3),
                                            150, 50, kAll);
  const FeltAction small = miranda.act(state);
  const FeltAction large = oliver.act(state);
  require(small.type == FELT_ACTION_RAISE_TO, "miranda did not raise");
  require(large.type == FELT_ACTION_RAISE_TO, "oliver did not raise");
  require(small.amount_to == state.min_raise_to,
          "miranda did not use the minimum raise");
  require(large.amount_to > small.amount_to,
          "oliver did not raise larger than the minimum");

  /* After the flop the two sizes are one and a half times the pot when
   * opening the betting, and four times the wager when answering one. A set
   * of queens is raised by the shared policy in both spots. */
  Builder played;
  played.post_blinds();
  played.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
  played.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
  const std::array<FeltCard, 5> board = {card(10, 2), card(5, 3), card(0, 1),
                                         0, 0};
  {
    FeltGameState unbet = played.state(FELT_STREET_FLOP,
                                       FELT_POSITION_BIG_BLIND,
                                       card(10, 0), card(10, 1),
                                       1000, 0, kNoBet);
    set_board(unbet, board, 3U);
    unbet.decision_random = 1;
    const FeltAction bet = oliver.act(unbet);
    require(bet.type == FELT_ACTION_RAISE_TO && bet.amount_to == 1500,
            "oliver did not bet one and a half times the pot");
    const FeltAction tiny = miranda.act(unbet);
    require(tiny.type == FELT_ACTION_RAISE_TO &&
                tiny.amount_to == unbet.min_raise_to,
            "miranda did not bet the minimum");
  }
  {
    Builder bet_into;
    bet_into.post_blinds();
    bet_into.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 250);
    bet_into.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 250);
    bet_into.add(FELT_POSITION_BUTTON, FELT_STREET_FLOP, FELT_EVENT_BET, 700);
    FeltGameState facing = bet_into.state(FELT_STREET_FLOP,
                                          FELT_POSITION_BIG_BLIND,
                                          card(10, 0), card(10, 1),
                                          1700, 700, kAll);
    set_board(facing, board, 3U);
    facing.decision_random = 1;
    const FeltAction raise = oliver.act(facing);
    require(raise.type == FELT_ACTION_RAISE_TO && raise.amount_to == 2800,
            "oliver did not raise four times the wager");
    const FeltAction least = miranda.act(facing);
    require(least.type == FELT_ACTION_RAISE_TO &&
                least.amount_to == facing.min_raise_to,
            "miranda did not raise the minimum");
  }
}

/* The shared baseline chart limps aces from the small blind. This pins that
 * deliberate property, because several archetypes inherit it. */
void test_chart_limps_aces(felt::NativeBotRunner& miranda) {
  Builder builder;
  builder.post_blinds();
  const FeltGameState state = builder.state(FELT_STREET_PREFLOP,
                                            FELT_POSITION_BUTTON,
                                            card(12, 0), card(12, 1),
                                            150, 50, kAll);
  expect(miranda, state, FELT_ACTION_CALL, "the baseline chart stopped limping aces");
}

/* Andy opens any two cards for a small raise, bets every unbet pot he has a
 * story for, and only steps down when someone bets into him. */
void test_aggressive_andy(felt::NativeBotRunner& bot) {
  Builder open;
  open.post_blinds();
  {
    /* Seven-deuce offsuit, first in: the chart folds this, Andy raises it. */
    const FeltGameState state = open.state(FELT_STREET_PREFLOP,
                                           FELT_POSITION_BUTTON,
                                           card(5, 0), card(0, 1),
                                           150, 50, kAll);
    const FeltAction result = bot.act(state);
    require(result.type == FELT_ACTION_RAISE_TO, "andy did not open 72o");
    require(result.amount_to == 200, "andy's open was not a small raise");
  }

  Builder three_bet;
  three_bet.post_blinds();
  three_bet.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 200);
  three_bet.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 800);
  {
    /* Raised once already, so the second decision is back on the chart. */
    const FeltGameState state = three_bet.state(FELT_STREET_PREFLOP,
                                                FELT_POSITION_BUTTON,
                                                card(5, 0), card(0, 1),
                                                1000, 600, kAll);
    expect(bot, state, FELT_ACTION_FOLD, "andy called a 3-bet with 72o");
  }

  /* Andy raised preflop and the big blind called. */
  Builder played;
  played.post_blinds();
  played.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE, 200);
  played.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 200);
  {
    /* In position with total air, checked to: he bets. */
    FeltGameState state = played.state(FELT_STREET_FLOP, FELT_POSITION_BUTTON,
                                       card(5, 0), card(0, 1), 400, 0, kNoBet);
    set_board(state, {card(12, 2), card(9, 3), card(7, 1), 0, 0}, 3U);
    expect(bot, state, FELT_ACTION_RAISE_TO, "andy checked back air in position");
  }
  {
    /* Same air, but a bet lands on him: he steps back to the default policy,
     * which folds air to a bet. */
    FeltGameState state = played.state(FELT_STREET_FLOP, FELT_POSITION_BUTTON,
                                       card(5, 0), card(0, 1), 700, 300, kAll);
    set_board(state, {card(12, 2), card(9, 3), card(7, 1), 0, 0}, 3U);
    expect(bot, state, FELT_ACTION_FOLD, "andy kept firing into a bet");
  }

  /* Andy raised from the small blind and is now out of position. */
  Builder out_of_position;
  out_of_position.post_blinds();
  out_of_position.add(FELT_POSITION_BUTTON, FELT_STREET_PREFLOP,
                      FELT_EVENT_RAISE, 200);
  out_of_position.add(FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP,
                      FELT_EVENT_CALL, 200);
  {
    /* Out of position with air, but he told the preflop story: he bets. */
    FeltGameState state = out_of_position.state(
        FELT_STREET_TURN, FELT_POSITION_BUTTON, card(5, 0), card(0, 1),
        400, 0, kNoBet);
    set_board(state, {card(12, 2), card(9, 3), card(7, 1), card(4, 0), 0}, 4U);
    expect(bot, state, FELT_ACTION_RAISE_TO, "andy gave up as the raiser");
  }
  {
    /* Bottom pair in an unbet pot is always a bet. */
    FeltGameState state = played.state(FELT_STREET_RIVER, FELT_POSITION_BUTTON,
                                       card(7, 0), card(0, 1), 400, 0, kNoBet);
    set_board(state, {card(12, 2), card(9, 3), card(7, 1), card(4, 0),
                      card(2, 2)}, 5U);
    expect(bot, state, FELT_ACTION_RAISE_TO, "andy checked a pair");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 15) {
    std::cerr << "expected the fourteen archetype bot library paths\n";
    return 2;
  }
  try {
    felt::NativeBotRunner nancy(argv[1]);
    felt::NativeBotRunner station(argv[2]);
    felt::NativeBotRunner patty(argv[3]);
    felt::NativeBotRunner charlie(argv[4]);
    felt::NativeBotRunner sarah(argv[5]);
    felt::NativeBotRunner thomas(argv[6]);
    felt::NativeBotRunner travis(argv[7]);
    felt::NativeBotRunner one_and_done(argv[8]);
    felt::NativeBotRunner sam(argv[9]);
    felt::NativeBotRunner terry(argv[10]);
    felt::NativeBotRunner miranda(argv[11]);
    felt::NativeBotRunner oliver(argv[12]);
    felt::NativeBotRunner andy(argv[13]);
    felt::NativeBotRunner chalamet(argv[14]);

    test_nitty_nancy(nancy);
    test_calling_station(station);
    test_passive_patty(patty);
    test_chasing_charlie(charlie);
    test_semi_bluff_sarah(sarah);
    test_trapping_thomas(thomas);
    test_trapping_thomas_turn(thomas);
    test_barrel_policies(travis, one_and_done);
    test_scared_sam(sam);
    test_tilted_terry(terry);
    test_check_raise_chalamet(chalamet);
    test_sizing_overrides(miranda, oliver);
    test_chart_limps_aces(miranda);
    test_aggressive_andy(andy);
  } catch (const std::exception& error) {
    std::cerr << "archetype_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

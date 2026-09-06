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
}

/* Charlie calls a draw at any price. */
void test_chasing_charlie(felt::NativeBotRunner& bot) {
  Builder builder;
  builder.post_blinds();
  FeltGameState state = builder.state(FELT_STREET_FLOP, FELT_POSITION_BIG_BLIND,
                                      card(6, 3), card(3, 3), 400, 3000, kAll);
  set_board(state, {card(12, 3), card(9, 3), card(2, 0), 0, 0}, 3U);
  expect(bot, state, FELT_ACTION_CALL, "charlie folded a flush draw");
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

/* Thomas checks strong hands rather than opening, then raises when bet into. */
void test_trapping_thomas(felt::NativeBotRunner& bot) {
  Builder builder;
  builder.post_blinds();
  const std::array<FeltCard, 5> board = {card(12, 2), card(9, 3), card(2, 0), 0, 0};
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
    expect(bot, state, FELT_ACTION_RAISE_TO, "thomas did not check-raise");
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

/* Sam folds top pair to a bet above 50 big blinds. */
void test_scared_sam(felt::NativeBotRunner& bot) {
  Builder builder;
  builder.post_blinds();
  FeltGameState state = builder.state(FELT_STREET_FLOP, FELT_POSITION_BIG_BLIND,
                                      card(12, 0), card(3, 1), 2000, 8000, kAll);
  set_board(state, {card(12, 2), card(9, 3), card(2, 0), 0, 0}, 3U);
  expect(bot, state, FELT_ACTION_FOLD, "sam called a large bet without a straight");
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
}

/* Randy needs two red cards; the black ace is folded. */
void test_red_randy(felt::NativeBotRunner& bot) {
  Builder builder;
  builder.post_blinds();
  {
    const FeltGameState state = builder.state(FELT_STREET_PREFLOP,
                                              FELT_POSITION_BUTTON,
                                              card(12, 0), card(12, 2),
                                              150, 50, kAll);
    expect(bot, state, FELT_ACTION_FOLD, "randy played black aces");
  }
  {
    const FeltGameState state = builder.state(FELT_STREET_PREFLOP,
                                              FELT_POSITION_BUTTON,
                                              card(12, 1), card(10, 3),
                                              150, 50, kAll);
    expect(bot, state, FELT_ACTION_RAISE_TO, "randy folded a red raising hand");
  }
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
    felt::NativeBotRunner randy(argv[11]);
    felt::NativeBotRunner miranda(argv[12]);
    felt::NativeBotRunner oliver(argv[13]);
    felt::NativeBotRunner andy(argv[14]);

    test_nitty_nancy(nancy);
    test_calling_station(station);
    test_passive_patty(patty);
    test_chasing_charlie(charlie);
    test_semi_bluff_sarah(sarah);
    test_trapping_thomas(thomas);
    test_barrel_policies(travis, one_and_done);
    test_scared_sam(sam);
    test_tilted_terry(terry);
    test_red_randy(randy);
    test_sizing_overrides(miranda, oliver);
    test_chart_limps_aces(miranda);
    test_aggressive_andy(andy);
  } catch (const std::exception& error) {
    std::cerr << "archetype_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

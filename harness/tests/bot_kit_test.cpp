#include "felt/bot_kit.h"
#include "felt/card.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

FeltCard card(std::string_view text) {
  return felt::card_from_string(text);
}

FeltMadeHand made(std::string_view first,
                  std::string_view second,
                  std::initializer_list<std::string_view> board_text) {
  const std::array<FeltCard, 2> hole{card(first), card(second)};
  std::array<FeltCard, 5> board{};
  std::size_t index = 0;
  for (const std::string_view text : board_text) {
    board[index++] = card(text);
  }
  return felt_made_hand(hole.data(), board.data(),
                        static_cast<std::uint8_t>(board_text.size()));
}

FeltBoardTexture texture(
    std::initializer_list<std::string_view> board_text) {
  std::array<FeltCard, 5> board{};
  std::size_t index = 0;
  for (const std::string_view text : board_text) {
    board[index++] = card(text);
  }
  return felt_board_texture(board.data(),
                            static_cast<std::uint8_t>(board_text.size()));
}

void test_made_categories() {
  require(made("Ah", "Kd", {"9c", "7s", "2h"}).category ==
              FELT_MADE_HIGH_CARD,
          "high-card classification failed");
  require(made("Ah", "Kd", {"Ac", "7s", "2h"}).category ==
              FELT_MADE_ONE_PAIR,
          "one-pair classification failed");
  require(made("Ah", "Kd", {"Ac", "Ks", "2h"}).category ==
              FELT_MADE_TWO_PAIR,
          "two-pair classification failed");
  require(made("7c", "7d", {"7s", "Kh", "2c"}).category ==
              FELT_MADE_TRIPS,
          "trips category classification failed");
  require(made("6c", "5d", {"4s", "3h", "2c"}).category ==
              FELT_MADE_STRAIGHT,
          "straight classification failed");
  require(made("Ah", "Kh", {"9h", "5h", "2h"}).category ==
              FELT_MADE_FLUSH,
          "flush classification failed");
  require(made("7c", "7d", {"7s", "Kh", "Kc"}).category ==
              FELT_MADE_FULL_HOUSE,
          "full-house classification failed");
  require(made("7c", "7d", {"7s", "7h", "Kc"}).category ==
              FELT_MADE_QUADS,
          "quads classification failed");
  require(made("Ah", "Kh", {"Qh", "Jh", "Th"}).category ==
              FELT_MADE_STRAIGHT_FLUSH,
          "straight-flush classification failed");
}

void test_pair_relations() {
  const FeltMadeHand top = made("As", "Kd", {"Ac", "7s", "2h"});
  require(top.pair_relation == FELT_PAIR_TOP &&
              top.hole_kicker_rank == 11U &&
              felt_is_top_pair_or_better(&top),
          "top-pair classification failed");
  require(made("7c", "Ad", {"Ks", "7h", "2c"}).pair_relation ==
              FELT_PAIR_MIDDLE,
          "middle-pair classification failed");
  const FeltMadeHand middle = made("7c", "Ad", {"Ks", "7h", "2c"});
  require(!felt_is_top_pair_or_better(&middle),
          "middle pair counted as top pair or better");
  require(made("2s", "Ad", {"Kc", "7h", "2c"}).pair_relation ==
              FELT_PAIR_BOTTOM,
          "bottom-pair classification failed");
  require(made("Ac", "Ad", {"Ks", "7h", "2c"}).pair_relation ==
              FELT_PAIR_OVERPAIR,
          "overpair classification failed");
  require(made("6c", "6d", {"Ks", "7h", "2c"}).pair_relation ==
              FELT_PAIR_UNDERPAIR,
          "underpair classification failed");

  const FeltMadeHand board_pair = made("Ac", "Qd", {"7s", "7h", "2c"});
  require(board_pair.pair_relation == FELT_PAIR_NONE &&
              board_pair.hole_kicker_rank == 12U,
          "board-pair classification failed");
}

void test_two_pair_kinds() {
  const FeltMadeHand both = made("Ah", "Kd", {"Ac", "Ks", "2h"});
  require(both.two_pair_kind == FELT_TWO_PAIR_BOTH_HOLE_CARDS &&
              felt_is_top_pair_or_better(&both),
          "two pair using both distinct hole cards was not identified");

  const FeltMadeHand over =
      made("Ac", "Ad", {"Ks", "Kh", "Qc", "7d", "2c"});
  require(over.two_pair_kind == FELT_TWO_PAIR_OVER &&
              felt_is_top_pair_or_better(&over),
          "over two pair made with a pocket pair was not identified");
  const FeltMadeHand middle =
      made("8c", "8d", {"Ks", "Kh", "Qc", "5d", "2c"});
  require(middle.two_pair_kind == FELT_TWO_PAIR_MIDDLE &&
              !felt_is_top_pair_or_better(&middle),
          "middle two pair made with a pocket pair was not identified");
  const FeltMadeHand under =
      made("4c", "4d", {"Ks", "Kh", "Qc", "8d", "5c"});
  require(under.two_pair_kind == FELT_TWO_PAIR_UNDER &&
              !felt_is_top_pair_or_better(&under),
          "under two pair made with a pocket pair was not identified");

  require(made("As", "7d", {"Kc", "Kh", "7s", "2h", "3c"})
              .two_pair_kind == FELT_TWO_PAIR_OVER,
          "over two pair made with one hole card was not identified");
  require(made("As", "8d", {"Kc", "Kh", "Qs", "8h", "2c"})
              .two_pair_kind == FELT_TWO_PAIR_MIDDLE,
          "middle two pair made with one hole card was not identified");
  require(made("As", "2d", {"Kc", "Kh", "Qs", "8h", "2c"})
              .two_pair_kind == FELT_TWO_PAIR_UNDER,
          "under two pair made with one hole card was not identified");

  const FeltMadeHand board_only =
      made("As", "Qd", {"Kc", "Kh", "7s", "7h", "2c"});
  require(board_only.two_pair_kind == FELT_TWO_PAIR_BOARD_ONLY &&
              !felt_is_top_pair_or_better(&board_only),
          "board-only two pair was not identified");

  /* The deuces are counterfeited: the best two pair is aces and kings, so
   * only the ace in the hole contributes. */
  require(made("As", "2d", {"Ac", "Kc", "Kh", "2h", "3c"})
              .two_pair_kind == FELT_TWO_PAIR_OVER,
          "counterfeited lower pair changed the best-five classification");
  require(made("As", "Kd", {"Ac", "7s", "2h"}).two_pair_kind ==
              FELT_TWO_PAIR_NONE,
          "non-two-pair hand received a two-pair subtype");
}

void test_set_trips_and_board_play() {
  const FeltMadeHand set = made("7c", "7d", {"7s", "Kh", "2c"});
  require(set.is_set && !set.is_trips, "set classification failed");

  const FeltMadeHand trips = made("7c", "Ad", {"7s", "7h", "Kc"});
  require(trips.is_trips && !trips.is_set, "trips classification failed");

  const FeltMadeHand plays =
      made("2c", "3d", {"Ah", "Kh", "Qh", "Jh", "Th"});
  require(plays.plays_board && !plays.improves_board,
          "playing-the-board classification failed");

  const FeltMadeHand improves =
      made("As", "Ad", {"Kh", "Qh", "Jh", "Th", "2c"});
  require(!improves.plays_board && improves.improves_board,
          "board improvement classification failed");
}

FeltDraws draws(std::string_view first,
                std::string_view second,
                std::initializer_list<std::string_view> board_text) {
  const std::array<FeltCard, 2> hole{card(first), card(second)};
  std::array<FeltCard, 5> board{};
  std::size_t index = 0;
  for (const std::string_view text : board_text) {
    board[index++] = card(text);
  }
  return felt_draws(hole.data(), board.data(),
                    static_cast<std::uint8_t>(board_text.size()));
}

void test_draws() {
  const FeltDraws flush = draws("Ah", "Kh", {"Qh", "7h", "2c"});
  require(flush.valid && (flush.flags & FELT_DRAW_FLUSH) != 0U &&
              (flush.flags & FELT_DRAW_OVERCARDS) != 0U &&
              flush.flush_next_cards == 9U &&
              flush.improving_next_cards == 15U && flush.nut_flush_draw,
          "nut flush draw classification failed");

  const FeltDraws open = draws("8c", "7d", {"6s", "5h", "Kc"});
  require((open.flags & FELT_DRAW_OPEN_ENDED) != 0U &&
              open.straight_next_cards == 8U,
          "open-ended straight draw classification failed");

  const FeltDraws gutshot = draws("8c", "7d", {"6s", "4h", "Kc"});
  require((gutshot.flags & FELT_DRAW_GUTSHOT) != 0U &&
              gutshot.straight_next_cards == 4U,
          "gutshot classification failed");

  const FeltDraws double_gutshot =
      draws("Tc", "8d", {"7s", "6h", "4c"});
  require((double_gutshot.flags & FELT_DRAW_DOUBLE_GUTSHOT) != 0U &&
              double_gutshot.straight_next_cards == 8U,
          "double-gutshot classification failed");

  const FeltDraws board_only =
      draws("8c", "2d", {"8s", "7h", "6c", "5d"});
  require((board_only.flags &
           (FELT_DRAW_GUTSHOT | FELT_DRAW_OPEN_ENDED |
            FELT_DRAW_DOUBLE_GUTSHOT)) == 0U,
          "shared board straight was counted as a private draw");

  const FeltDraws river =
      draws("Ah", "Kh", {"Qh", "7h", "2c", "3d", "4s"});
  require(river.valid && river.flags == FELT_DRAW_NONE &&
              river.improving_next_cards == 0U,
          "river draw was not treated as busted air");
}

void test_board_texture() {
  const FeltBoardTexture dry = texture({"Kc", "7d", "2s"});
  require(dry.valid && dry.high_rank == 11U && dry.broadway_count == 1U &&
              dry.distinct_rank_count == 3U && dry.max_suit_count == 1U &&
              dry.pair_count == 0U &&
              dry.max_cards_in_five_rank_window == 1U,
          "dry-board texture failed");

  const FeltBoardTexture connected = texture({"9h", "8h", "7c"});
  require(connected.max_suit_count == 2U &&
              connected.max_cards_in_five_rank_window == 3U,
          "connected two-tone texture failed");

  const FeltBoardTexture paired = texture({"Kc", "Kd", "2s"});
  require(paired.pair_count == 1U && !paired.trips_on_board &&
              !paired.quads_on_board,
          "paired-board texture failed");

  const FeltBoardTexture two_pair =
      texture({"Kc", "Kd", "2s", "2h", "Ac"});
  require(two_pair.pair_count == 2U,
          "two-pair-board texture failed");

  const FeltBoardTexture quads =
      texture({"7c", "7d", "7s", "7h", "2c"});
  require(quads.quads_on_board && !quads.trips_on_board,
          "quads-on-board texture failed");

  const FeltBoardTexture wheel =
      texture({"Ac", "2d", "3s", "4h", "5c"});
  require(wheel.straight_on_board &&
              wheel.max_cards_in_five_rank_window == 5U,
          "wheel-board texture failed");

  const FeltBoardTexture flush =
      texture({"Ah", "Jh", "9h", "5h", "2h"});
  require(flush.flush_on_board && flush.max_suit_count == 5U,
          "flush-board texture failed");
}

void test_invalid_inputs() {
  const std::array<FeltCard, 2> duplicate_hole{card("Ac"), card("Ac")};
  const std::array<FeltCard, 3> board{card("2c"), card("3d"), card("4s")};
  require(!felt_made_hand(duplicate_hole.data(), board.data(), 3U).valid,
          "duplicate hole cards were accepted");

  const std::array<FeltCard, 2> hole{card("Ac"), card("Kd")};
  require(!felt_made_hand(hole.data(), board.data(), 2U).valid,
          "preflop-sized board was accepted");
  require(!felt_board_texture(nullptr, 3U).valid,
          "null board was accepted");
}

FeltPreflopChartAction chart(FeltPreflopSpot spot,
                             std::string_view first,
                             std::string_view second) {
  return felt_preflop_baseline_lookup(spot, card(first), card(second));
}

void test_preflop_classes_and_ranges() {
  require(felt_preflop_class(card("Ac"), card("Ad")) == 168U,
          "AA preflop class changed");
  require(felt_preflop_class(card("Ah"), card("Kh")) == 155U,
          "AKs preflop class changed");
  require(felt_preflop_class(card("Ac"), card("Kd")) == 167U,
          "AKo preflop class changed");
  require(felt_preflop_class(card("Ac"), card("Ac")) ==
              FELT_INVALID_PREFLOP_CLASS,
          "duplicate preflop cards were accepted");

  require(chart(FELT_PREFLOP_SB_FIRST_IN, "Ah", "Kh") ==
              FELT_PREFLOP_CHART_RAISE_VALUE,
          "SB AKs should raise for value");
  require(chart(FELT_PREFLOP_SB_FIRST_IN, "Ah", "5h") ==
              FELT_PREFLOP_CHART_RAISE_VALUE,
          "SB A5s should raise for value");
  require(chart(FELT_PREFLOP_SB_FIRST_IN, "Ac", "6d") ==
              FELT_PREFLOP_CHART_RAISE_BLUFF,
          "SB A6o should raise as a bluff");
  require(chart(FELT_PREFLOP_SB_FIRST_IN, "Qc", "5d") ==
              FELT_PREFLOP_CHART_RAISE_BLUFF,
          "SB Q5o should raise as a bluff");
  require(chart(FELT_PREFLOP_SB_FIRST_IN, "Jc", "2d") ==
              FELT_PREFLOP_CHART_FOLD,
          "SB J2o should fold");

  require(chart(FELT_PREFLOP_BB_VS_SMALL_RAISE, "Ac", "Ad") ==
              FELT_PREFLOP_CHART_RAISE_VALUE,
          "BB AA should 3-bet for value");
  require(chart(FELT_PREFLOP_BB_VS_SMALL_RAISE, "Jh", "9h") ==
              FELT_PREFLOP_CHART_RAISE_BLUFF,
          "BB J9s should 3-bet as a bluff");
  require(chart(FELT_PREFLOP_BB_VS_SMALL_RAISE, "Ah", "5h") ==
              FELT_PREFLOP_CHART_RAISE_BLUFF,
          "BB A5s should 3-bet as a bluff");
  require(chart(FELT_PREFLOP_BB_VS_SMALL_RAISE, "Ac", "2d") ==
              FELT_PREFLOP_CHART_PASSIVE,
          "BB A2o should call rather than bluff");
  require(chart(FELT_PREFLOP_BB_VS_SB_LIMP, "7c", "7d") ==
              FELT_PREFLOP_CHART_RAISE_VALUE,
          "BB 77 should raise a limp for value");
  require(chart(FELT_PREFLOP_BB_VS_SB_LIMP, "Ah", "5h") ==
              FELT_PREFLOP_CHART_RAISE_BLUFF,
          "BB A5s should bluff-raise a limp");
  require(chart(FELT_PREFLOP_BB_VS_SB_LIMP, "Ac", "2d") ==
              FELT_PREFLOP_CHART_PASSIVE,
          "BB A2o should check a limp");
  require(chart(FELT_PREFLOP_BB_VS_SMALL_RAISE, "7c", "2d") ==
              FELT_PREFLOP_CHART_FOLD,
          "BB 72o should fold to an open");

  require(chart(FELT_PREFLOP_VS_MEDIUM_RAISE, "Ah", "Kh") ==
              FELT_PREFLOP_CHART_RAISE_VALUE,
          "AKs should 4-bet for value");
  require(chart(FELT_PREFLOP_VS_MEDIUM_RAISE, "Ah", "5h") ==
              FELT_PREFLOP_CHART_RAISE_BLUFF,
          "A5s should 4-bet as a bluff");
  require(chart(FELT_PREFLOP_VS_MEDIUM_RAISE, "Jh", "4h") ==
              FELT_PREFLOP_CHART_FOLD,
          "J4s should fold to a medium raise");
  require(chart(FELT_PREFLOP_VS_MEDIUM_RAISE, "Ah", "Th") ==
              FELT_PREFLOP_CHART_PASSIVE,
          "ATs should call a 3-bet");
  require(chart(FELT_PREFLOP_VS_MEDIUM_RAISE, "Th", "5h") ==
              FELT_PREFLOP_CHART_FOLD,
          "T5s should fold to a 3-bet");

  require(chart(FELT_PREFLOP_SB_VS_SMALL_RAISE, "Ac", "Ad") ==
              FELT_PREFLOP_CHART_RAISE_VALUE,
          "limped AA should re-raise for value");
  require(chart(FELT_PREFLOP_SB_VS_SMALL_RAISE, "Qc", "7d") ==
              FELT_PREFLOP_CHART_FOLD,
          "Q7o should fold to a small raise");
  require(chart(FELT_PREFLOP_SB_VS_SMALL_RAISE, "Kc", "4d") ==
              FELT_PREFLOP_CHART_FOLD,
          "K4o should limp-fold");
  require(chart(FELT_PREFLOP_SB_VS_SMALL_RAISE, "7h", "6h") ==
              FELT_PREFLOP_CHART_RAISE_BLUFF,
          "76s should re-raise as a bluff");
  require(chart(FELT_PREFLOP_SB_VS_SMALL_RAISE, "8h", "7h") ==
              FELT_PREFLOP_CHART_RAISE_BLUFF,
          "87s should re-raise as a bluff");
  require(chart(FELT_PREFLOP_SB_VS_SMALL_RAISE, "Kc", "2d") ==
              FELT_PREFLOP_CHART_FOLD,
          "K2o should fold to a small raise");
  require(chart(FELT_PREFLOP_SB_VS_SMALL_RAISE, "Ac", "Jd") ==
              FELT_PREFLOP_CHART_PASSIVE,
          "AJo should call a small raise");

  require(chart(FELT_PREFLOP_VS_LARGE_RAISE, "Kc", "Kd") ==
              FELT_PREFLOP_CHART_ALL_IN,
          "KK should shove over a 4-bet");
  require(chart(FELT_PREFLOP_VS_LARGE_RAISE, "Jc", "Jd") ==
              FELT_PREFLOP_CHART_PASSIVE,
          "JJ should call a 4-bet");
  require(chart(FELT_PREFLOP_VS_LARGE_RAISE, "6h", "5h") ==
              FELT_PREFLOP_CHART_PASSIVE,
          "65s should call a large raise");
  require(chart(FELT_PREFLOP_VS_ALL_IN_SIZED_RAISE, "Qc", "Qd") ==
              FELT_PREFLOP_CHART_ALL_IN,
          "QQ should jam over an all-in-sized raise");
}

void test_preflop_combo_counts() {
  using Counts = std::array<std::uint32_t, 5>;
  const auto counts_for = [](FeltPreflopSpot spot) {
    Counts counts{};
    for (FeltCard first = 0; first < 52U; ++first) {
      for (FeltCard second = static_cast<FeltCard>(first + 1U); second < 52U;
           ++second) {
        const auto action =
            felt_preflop_baseline_lookup(spot, first, second);
        ++counts[static_cast<std::size_t>(action)];
      }
    }
    return counts;
  };

  require(counts_for(FELT_PREFLOP_SB_FIRST_IN) ==
              Counts{392U, 566U, 184U, 184U, 0U},
          "SB first-in combo counts changed");
  require(counts_for(FELT_PREFLOP_BB_VS_SMALL_RAISE) ==
              Counts{288U, 822U, 124U, 92U, 0U},
          "BB-vs-open combo counts changed");
  require(counts_for(FELT_PREFLOP_BB_VS_SB_LIMP) ==
              Counts{0U, 1134U, 136U, 56U, 0U},
          "BB-vs-limp combo counts changed");
  require(counts_for(FELT_PREFLOP_SB_VS_SMALL_RAISE) ==
              Counts{596U, 510U, 160U, 60U, 0U},
          "SB-vs-small-raise combo counts changed");
  require(counts_for(FELT_PREFLOP_VS_MEDIUM_RAISE) ==
              Counts{1056U, 184U, 66U, 20U, 0U},
          "medium-raise response combo counts changed");
  require(counts_for(FELT_PREFLOP_VS_LARGE_RAISE) ==
              Counts{1228U, 64U, 0U, 0U, 34U},
          "4-bet response combo counts changed");
  require(counts_for(FELT_PREFLOP_VS_ALL_IN_SIZED_RAISE) ==
              Counts{1292U, 0U, 0U, 0U, 34U},
          "all-in response combo counts changed");
}

FeltGameState preflop_state(std::uint32_t position,
                            FeltCard first,
                            FeltCard second,
                            const FeltActionEvent* history,
                            std::uint32_t history_count) {
  FeltGameState state{};
  state.abi_version = FELT_BOT_ABI_VERSION;
  state.struct_size = sizeof(FeltGameState);
  state.hole[0] = first;
  state.hole[1] = second;
  state.board_count = 0U;
  state.street = FELT_STREET_PREFLOP;
  state.position = position;
  state.legal_actions =
      FELT_LEGAL_FOLD | FELT_LEGAL_CALL | FELT_LEGAL_RAISE_TO;
  state.my_stack = 19900;
  state.opp_stack = 19750;
  state.min_raise_to = 200;
  state.max_raise_to = 20000;
  state.history = history;
  state.history_count = history_count;
  return state;
}

void test_preflop_spot_recognition_and_actions() {
  const FeltActionEvent blinds[] = {
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP,
       FELT_EVENT_POST_SMALL_BLIND, 0U, 50},
      {FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP,
       FELT_EVENT_POST_BIG_BLIND, 0U, 100}};

  FeltGameState first_in =
      preflop_state(FELT_POSITION_BUTTON, card("Ah"), card("Kh"), blinds, 2U);
  first_in.my_street_contribution = 50;
  first_in.opp_street_contribution = 100;
  first_in.to_call = 50;
  const FeltPreflopDecision first_decision =
      felt_preflop_baseline_decision(&first_in);
  const FeltAction first_action = felt_preflop_baseline_action(&first_in);
  require(first_decision.valid &&
              first_decision.spot == FELT_PREFLOP_SB_FIRST_IN &&
              first_action.type == FELT_ACTION_RAISE_TO &&
              first_action.amount_to == 250,
          "SB first-in action failed");

  const FeltActionEvent opened[] = {
      blinds[0], blinds[1],
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 250}};
  FeltGameState versus_open = preflop_state(
      FELT_POSITION_BIG_BLIND, card("Jh"), card("9h"), opened, 3U);
  versus_open.my_street_contribution = 100;
  versus_open.opp_street_contribution = 250;
  versus_open.to_call = 150;
  versus_open.min_raise_to = 400;
  const FeltAction three_bet = felt_preflop_baseline_action(&versus_open);
  require(felt_preflop_baseline_decision(&versus_open).spot ==
              FELT_PREFLOP_BB_VS_SMALL_RAISE &&
              three_bet.type == FELT_ACTION_RAISE_TO &&
              three_bet.amount_to == 1000,
          "BB 3-bet sizing failed");

  const FeltActionEvent three_bet_history[] = {
      blinds[0], blinds[1], opened[2],
      {FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 1000}};
  FeltGameState facing_three_bet = preflop_state(
      FELT_POSITION_BUTTON, card("Ah"), card("Th"), three_bet_history, 4U);
  facing_three_bet.my_street_contribution = 250;
  facing_three_bet.opp_street_contribution = 1000;
  facing_three_bet.to_call = 750;
  facing_three_bet.min_raise_to = 1750;
  require(felt_preflop_baseline_decision(&facing_three_bet).spot ==
              FELT_PREFLOP_VS_MEDIUM_RAISE &&
              felt_preflop_baseline_action(&facing_three_bet).type ==
                  FELT_ACTION_CALL,
          "SB call versus 3-bet failed");

  const FeltActionEvent limp_raised[] = {
      blinds[0], blinds[1],
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_CALL, 0U, 100},
      {FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 400}};
  FeltGameState limp_reraise = preflop_state(
      FELT_POSITION_BUTTON, card("Ac"), card("Ad"), limp_raised, 4U);
  limp_reraise.my_street_contribution = 100;
  limp_reraise.opp_street_contribution = 400;
  limp_reraise.to_call = 300;
  limp_reraise.min_raise_to = 700;
  const FeltAction reraise = felt_preflop_baseline_action(&limp_reraise);
  require(felt_preflop_baseline_decision(&limp_reraise).spot ==
              FELT_PREFLOP_SB_VS_SMALL_RAISE &&
              reraise.type == FELT_ACTION_RAISE_TO &&
              reraise.amount_to == 1200,
          "SB limp re-raise sizing failed");

  const FeltActionEvent forty_bb_open[] = {
      blinds[0], blinds[1],
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 4000}};
  FeltGameState versus_forty = preflop_state(
      FELT_POSITION_BIG_BLIND, card("Kc"), card("Kd"), forty_bb_open, 3U);
  versus_forty.my_street_contribution = 100;
  versus_forty.opp_street_contribution = 4000;
  versus_forty.to_call = 3900;
  versus_forty.min_raise_to = 7900;
  const FeltPreflopDecision forty_decision =
      felt_preflop_baseline_decision(&versus_forty);
  require(forty_decision.spot == FELT_PREFLOP_VS_LARGE_RAISE &&
              felt_preflop_baseline_action(&versus_forty).type ==
                  FELT_ACTION_RAISE_TO &&
              felt_preflop_baseline_action(&versus_forty).amount_to == 20000,
          "40 bb first raise did not use the large-raise chart");
  require(felt_preflop_action_count_v0_decision(&versus_forty).spot ==
              FELT_PREFLOP_BB_VS_SMALL_RAISE,
          "action-count v0 did not preserve first-raise routing");

  FeltActionEvent boundary_history[] = {
      blinds[0], blinds[1],
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 1099}};
  FeltGameState boundary = preflop_state(
      FELT_POSITION_BIG_BLIND, card("Ac"), card("Ad"), boundary_history, 3U);
  boundary.my_street_contribution = 100;
  const std::array<std::pair<FeltChips, FeltPreflopSpot>, 6> boundaries{{
      {599, FELT_PREFLOP_BB_VS_SMALL_RAISE},
      {600, FELT_PREFLOP_VS_MEDIUM_RAISE},
      {2199, FELT_PREFLOP_VS_MEDIUM_RAISE},
      {2200, FELT_PREFLOP_VS_LARGE_RAISE},
      {4499, FELT_PREFLOP_VS_LARGE_RAISE},
      {4500, FELT_PREFLOP_VS_ALL_IN_SIZED_RAISE},
  }};
  for (const auto& [amount, expected] : boundaries) {
    boundary_history[2].amount_to = amount + 100;
    boundary.opp_street_contribution = amount + 100;
    boundary.to_call = amount;
    require(felt_preflop_baseline_decision(&boundary).spot == expected,
            "call-size bucket boundary changed");
  }

  const FeltActionEvent repeated_min_raises[] = {
      blinds[0], blinds[1],
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 250},
      {FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 400},
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 1200},
      {FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 2000},
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 4800},
      {FELT_POSITION_BIG_BLIND, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 7600}};
  FeltGameState facing_late_min_raise = preflop_state(
      FELT_POSITION_BUTTON, card("Jc"), card("Jd"), repeated_min_raises, 8U);
  facing_late_min_raise.my_street_contribution = 4800;
  facing_late_min_raise.opp_street_contribution = 7600;
  facing_late_min_raise.to_call = 2800;
  facing_late_min_raise.min_raise_to = 10400;
  require(felt_preflop_baseline_decision(&facing_late_min_raise).spot ==
              FELT_PREFLOP_VS_LARGE_RAISE &&
              felt_preflop_baseline_action(&facing_late_min_raise).type !=
                  FELT_ACTION_FOLD,
          "late minimum raise ignored the remaining call size");

  const FeltActionEvent twenty_bb_shove[] = {
      blinds[0], blinds[1],
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 2000}};
  FeltGameState short_shove_bluff = preflop_state(
      FELT_POSITION_BIG_BLIND, card("Ah"), card("5h"), twenty_bb_shove, 3U);
  short_shove_bluff.legal_actions = FELT_LEGAL_FOLD | FELT_LEGAL_CALL;
  short_shove_bluff.opp_stack = 0;
  short_shove_bluff.opp_street_contribution = 2000;
  short_shove_bluff.to_call = 1900;
  require(felt_preflop_baseline_decision(&short_shove_bluff).spot ==
              FELT_PREFLOP_VS_MEDIUM_RAISE &&
              felt_preflop_baseline_action(&short_shove_bluff).type ==
                  FELT_ACTION_FOLD,
          "medium-size bluff raise became a call versus a short all-in");

  FeltGameState short_shove_value = short_shove_bluff;
  short_shove_value.hole[0] = card("Ac");
  short_shove_value.hole[1] = card("Ad");
  require(felt_preflop_baseline_action(&short_shove_value).type ==
              FELT_ACTION_CALL,
          "medium-size value raise did not call a short all-in");

  const FeltActionEvent forty_six_bb_raise[] = {
      blinds[0], blinds[1],
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 4600}};
  FeltGameState versus_forty_six = preflop_state(
      FELT_POSITION_BIG_BLIND, card("Qc"), card("Qd"), forty_six_bb_raise, 3U);
  versus_forty_six.my_street_contribution = 100;
  versus_forty_six.opp_street_contribution = 4600;
  versus_forty_six.to_call = 4500;
  versus_forty_six.min_raise_to = 9100;
  const FeltAction forty_six_action =
      felt_preflop_baseline_action(&versus_forty_six);
  require(felt_preflop_baseline_decision(&versus_forty_six).spot ==
              FELT_PREFLOP_VS_ALL_IN_SIZED_RAISE &&
              forty_six_action.type == FELT_ACTION_RAISE_TO &&
              forty_six_action.amount_to == 20000,
          "QQ flat-called a 46 bb raise instead of jamming");
  FeltGameState folded_forty_six = versus_forty_six;
  folded_forty_six.hole[0] = card("7c");
  folded_forty_six.hole[1] = card("2d");
  require(felt_preflop_baseline_action(&folded_forty_six).type ==
              FELT_ACTION_FOLD,
          "72o did not fold to a 46 bb raise");

  const FeltActionEvent all_in_history[] = {
      blinds[0], blinds[1],
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 7600}};
  FeltGameState facing_all_in = preflop_state(
      FELT_POSITION_BIG_BLIND, card("Qc"), card("Qd"), all_in_history, 3U);
  facing_all_in.legal_actions = FELT_LEGAL_FOLD | FELT_LEGAL_CALL;
  facing_all_in.opp_stack = 0;
  facing_all_in.opp_street_contribution = 7600;
  facing_all_in.to_call = 7500;
  const FeltPreflopDecision all_in_decision =
      felt_preflop_baseline_decision(&facing_all_in);
  require(all_in_decision.spot ==
              FELT_PREFLOP_VS_ALL_IN_SIZED_RAISE &&
              felt_preflop_baseline_action(&facing_all_in).type ==
                  FELT_ACTION_CALL,
          "all-in response failed");

  const FeltActionEvent full_shove_history[] = {
      blinds[0], blinds[1],
      {FELT_POSITION_BUTTON, FELT_STREET_PREFLOP, FELT_EVENT_RAISE,
       0U, 20000}};
  FeltGameState full_shove = preflop_state(
      FELT_POSITION_BIG_BLIND, card("Jh"), card("9h"), full_shove_history,
      3U);
  full_shove.legal_actions = FELT_LEGAL_FOLD | FELT_LEGAL_CALL;
  full_shove.opp_stack = 0;
  full_shove.opp_street_contribution = 20000;
  full_shove.to_call = 19900;
  require(felt_preflop_baseline_action(&full_shove).type ==
              FELT_ACTION_FOLD &&
              felt_preflop_action_count_v0_decision(&full_shove).spot ==
                  FELT_PREFLOP_BB_VS_SMALL_RAISE &&
              felt_preflop_action_count_v0_action(&full_shove).type ==
                  FELT_ACTION_CALL,
          "action-count v0 no longer treats an opening shove as small");
}

}  // namespace

int main() {
  try {
    test_made_categories();
    test_pair_relations();
    test_two_pair_kinds();
    test_set_trips_and_board_play();
    test_draws();
    test_board_texture();
    test_invalid_inputs();
    test_preflop_classes_and_ranges();
    test_preflop_combo_counts();
    test_preflop_spot_recognition_and_actions();
  } catch (const std::exception& error) {
    std::cerr << "bot_kit_test: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

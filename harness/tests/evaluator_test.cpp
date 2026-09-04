#include "felt/card.hpp"
#include "felt/evaluator.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

felt::HandRank rank(
    const std::array<std::string_view, 7>& card_strings) {
  std::array<felt::Card, 7> cards{};
  for (std::size_t index = 0; index < cards.size(); ++index) {
    cards[index] = felt::card_from_string(card_strings[index]);
  }
  return felt::evaluate7(cards);
}

void require_category(const std::array<std::string_view, 7>& cards,
                      std::uint8_t expected,
                      const std::string& message) {
  require(felt::hand_category(rank(cards)) == expected, message);
}

}  // namespace

int main() {
  try {
    require_category({"Ah", "Kh", "Qh", "Jh", "Th", "2c", "3d"}, 9,
                     "straight flush category failed");
    require_category({"Ac", "Ad", "As", "Ah", "Kc", "Qd", "2s"}, 8,
                     "quads category failed");
    require_category({"Ac", "Ad", "Ah", "Kc", "Kd", "2s", "3h"}, 7,
                     "full house category failed");
    require_category({"Ah", "Jh", "9h", "5h", "2h", "Kc", "Qd"}, 6,
                     "flush category failed");
    require_category({"Ac", "2d", "3s", "4h", "5c", "Kd", "Qd"}, 5,
                     "wheel category failed");
    require_category({"Ac", "Ad", "Ah", "Kc", "Qd", "2s", "3h"}, 4,
                     "trips category failed");
    require_category({"Ac", "Ad", "Kc", "Kd", "Qc", "2d", "3s"}, 3,
                     "two-pair category failed");
    require_category({"Ac", "Ad", "Kc", "Qd", "Js", "2h", "3c"}, 2,
                     "pair category failed");
    require_category({"Ac", "Kd", "Qs", "Jh", "9c", "2d", "3s"}, 1,
                     "high-card category failed");

    require(rank({"2c", "3d", "4s", "5h", "6c", "Kd", "Qd"}) >
                rank({"Ac", "2d", "3s", "4h", "5c", "Kd", "Qd"}),
            "six-high straight did not beat the wheel");
    require(rank({"Ac", "Ad", "Kc", "Qd", "Js", "2h", "3c"}) >
                rank({"Ac", "Ad", "Qc", "Jd", "Ts", "2h", "3c"}),
            "pair kicker ordering failed");
    require(rank({"Ac", "Kd", "Qs", "Jh", "9c", "2d", "3s"}) ==
                rank({"Ah", "Ks", "Qd", "Jc", "9h", "2s", "3d"}),
            "suit permutation changed hand rank");

    try {
      (void)rank({"Ac", "Ac", "Qs", "Jh", "9c", "2d", "3s"});
      throw std::runtime_error("duplicate cards were accepted");
    } catch (const std::invalid_argument&) {
    }
  } catch (const std::exception& error) {
    std::cerr << "evaluator_test: " << error.what() << '\n';
    return 1;
  }

  return 0;
}

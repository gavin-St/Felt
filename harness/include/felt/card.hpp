#ifndef FELT_CARD_HPP
#define FELT_CARD_HPP

#include "felt/bot_api.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace felt {

using Card = FeltCard;

constexpr std::uint8_t kRankCount = 13;
constexpr std::uint8_t kSuitCount = 4;
constexpr std::uint8_t kCardCount = 52;

[[nodiscard]] constexpr bool is_valid_card(Card card) noexcept {
  return card < kCardCount;
}

[[nodiscard]] constexpr std::uint8_t card_rank(Card card) noexcept {
  return static_cast<std::uint8_t>(card >> 2U);
}

[[nodiscard]] constexpr std::uint8_t card_suit(Card card) noexcept {
  return static_cast<std::uint8_t>(card & 3U);
}

[[nodiscard]] std::string card_to_string(Card card);
[[nodiscard]] Card card_from_string(std::string_view text);

}  // namespace felt

#endif

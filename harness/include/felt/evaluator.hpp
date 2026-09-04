#ifndef FELT_EVALUATOR_HPP
#define FELT_EVALUATOR_HPP

#include "felt/card.hpp"

#include <array>
#include <cstdint>

namespace felt {

using HandRank = std::uint16_t;

[[nodiscard]] HandRank evaluate7(const std::array<Card, 7>& cards);
[[nodiscard]] HandRank evaluate7_unchecked(const std::array<Card, 7>& cards);
[[nodiscard]] constexpr std::uint8_t hand_category(HandRank rank) noexcept {
  return static_cast<std::uint8_t>(rank >> 12U);
}

}  // namespace felt

#endif

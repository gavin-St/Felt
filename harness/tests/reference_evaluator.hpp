#ifndef FELT_TESTS_REFERENCE_EVALUATOR_HPP
#define FELT_TESTS_REFERENCE_EVALUATOR_HPP

#include "felt/card.hpp"

#include <array>
#include <cstdint>

namespace felt::tests {

using ReferenceRank = std::uint32_t;

[[nodiscard]] ReferenceRank reference_evaluate7(
    const std::array<Card, 7>& cards);
[[nodiscard]] constexpr std::uint8_t reference_category(
    ReferenceRank rank) noexcept {
  return static_cast<std::uint8_t>(rank >> 24U);
}

}  // namespace felt::tests

#endif

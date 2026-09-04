#ifndef FELT_EQUITY_HPP
#define FELT_EQUITY_HPP

#include "felt/hand_engine.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace felt {

struct EquityCounts {
  std::uint64_t boards{};
  std::array<std::uint64_t, 2> wins{};
  std::uint64_t ties{};
};

class ExactEquityCalculator {
 public:
  [[nodiscard]] EquityCounts calculate(const HandCards& cards,
                                       std::uint32_t all_in_street);

  [[nodiscard]] std::size_t preflop_cache_size() const noexcept;
  [[nodiscard]] std::uint64_t preflop_cache_hits() const noexcept;
  [[nodiscard]] std::uint64_t preflop_cache_misses() const noexcept;

 private:
  std::unordered_map<std::uint32_t, EquityCounts> preflop_cache_;
  std::uint64_t preflop_cache_hits_{};
  std::uint64_t preflop_cache_misses_{};
};

[[nodiscard]] std::array<FeltChips, 2> equity_payout(
    FeltChips pot,
    const EquityCounts& equity);

void apply_equity_adjustment(HandResult& result,
                             const HandCards& cards,
                             FeltChips starting_stack,
                             ExactEquityCalculator& calculator);

}  // namespace felt

#endif

#ifndef FELT_RANDOM_HPP
#define FELT_RANDOM_HPP

#include "felt/card.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace felt {

using Sha256Digest = std::array<std::uint8_t, 32>;
using Deck = std::array<Card, kCardCount>;

[[nodiscard]] Sha256Digest sha256(const std::uint8_t* data, std::size_t size);
[[nodiscard]] Sha256Digest sha256(std::string_view text);
[[nodiscard]] Sha256Digest derive_seed(
    std::string_view domain,
    std::initializer_list<std::uint64_t> values);

class Xoshiro256PlusPlus {
 public:
  explicit Xoshiro256PlusPlus(const Sha256Digest& seed) noexcept;
  explicit Xoshiro256PlusPlus(std::array<std::uint64_t, 4> state) noexcept;

  [[nodiscard]] std::uint64_t next() noexcept;
  [[nodiscard]] std::uint64_t bounded(std::uint64_t bound);
  [[nodiscard]] const std::array<std::uint64_t, 4>& state() const noexcept;

 private:
  std::array<std::uint64_t, 4> state_;
};

[[nodiscard]] Sha256Digest deal_seed(std::uint64_t match_seed,
                                     std::uint64_t deal_index);
[[nodiscard]] Deck shuffled_deck(std::uint64_t match_seed,
                                 std::uint64_t deal_index);
[[nodiscard]] std::uint64_t decision_random(std::uint64_t match_seed,
                                            std::uint64_t randomness_index,
                                            std::uint64_t decision_index,
                                            std::uint32_t acting_position);

}  // namespace felt

#endif

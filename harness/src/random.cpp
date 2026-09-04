#include "felt/random.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace felt {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256Constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

constexpr std::array<std::uint32_t, 8> kSha256Initial{
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

constexpr std::uint32_t rotate_right(std::uint32_t value,
                                     unsigned amount) noexcept {
  return (value >> amount) | (value << (32U - amount));
}

constexpr std::uint64_t rotate_left(std::uint64_t value,
                                    unsigned amount) noexcept {
  return (value << amount) | (value >> (64U - amount));
}

std::uint32_t load_big_endian_u32(const std::uint8_t* bytes) noexcept {
  return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
         (static_cast<std::uint32_t>(bytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[2]) << 8U) |
         static_cast<std::uint32_t>(bytes[3]);
}

std::uint64_t load_little_endian_u64(const std::uint8_t* bytes) noexcept {
  std::uint64_t value = 0;
  for (unsigned index = 0; index < 8U; ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return value;
}

class Sha256 {
 public:
  void update(const std::uint8_t* data, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
      buffer_[buffer_size_++] = data[index];
      if (buffer_size_ == buffer_.size()) {
        transform(buffer_.data());
        total_size_ += buffer_.size();
        buffer_size_ = 0;
      }
    }
  }

  Sha256Digest finish() {
    const std::uint64_t bit_length =
        static_cast<std::uint64_t>(total_size_ + buffer_size_) * 8U;

    buffer_[buffer_size_++] = 0x80U;
    if (buffer_size_ > 56U) {
      std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_),
                buffer_.end(), 0U);
      transform(buffer_.data());
      buffer_size_ = 0;
    }

    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_),
              buffer_.begin() + 56, 0U);
    for (unsigned index = 0; index < 8U; ++index) {
      buffer_[63U - index] =
          static_cast<std::uint8_t>(bit_length >> (index * 8U));
    }
    transform(buffer_.data());

    Sha256Digest digest{};
    for (std::size_t word = 0; word < state_.size(); ++word) {
      digest[word * 4U] = static_cast<std::uint8_t>(state_[word] >> 24U);
      digest[word * 4U + 1U] = static_cast<std::uint8_t>(state_[word] >> 16U);
      digest[word * 4U + 2U] = static_cast<std::uint8_t>(state_[word] >> 8U);
      digest[word * 4U + 3U] = static_cast<std::uint8_t>(state_[word]);
    }
    return digest;
  }

 private:
  void transform(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16U; ++index) {
      words[index] = load_big_endian_u32(block + index * 4U);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
      const std::uint32_t s0 = rotate_right(words[index - 15U], 7U) ^
                               rotate_right(words[index - 15U], 18U) ^
                               (words[index - 15U] >> 3U);
      const std::uint32_t s1 = rotate_right(words[index - 2U], 17U) ^
                               rotate_right(words[index - 2U], 19U) ^
                               (words[index - 2U] >> 10U);
      words[index] =
          words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t index = 0; index < words.size(); ++index) {
      const std::uint32_t sum1 = rotate_right(e, 6U) ^
                                 rotate_right(e, 11U) ^
                                 rotate_right(e, 25U);
      const std::uint32_t choice = (e & f) ^ (~e & g);
      const std::uint32_t temp1 =
          h + sum1 + choice + kSha256Constants[index] + words[index];
      const std::uint32_t sum0 = rotate_right(a, 2U) ^
                                 rotate_right(a, 13U) ^
                                 rotate_right(a, 22U);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = sum0 + majority;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_ = kSha256Initial;
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffer_size_{0};
  std::size_t total_size_{0};
};

void update_little_endian_u64(Sha256& hash, std::uint64_t value) {
  std::array<std::uint8_t, 8> bytes{};
  for (unsigned index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
  hash.update(bytes.data(), bytes.size());
}

}  // namespace

Sha256Digest sha256(const std::uint8_t* data, std::size_t size) {
  if (data == nullptr && size != 0U) {
    throw std::invalid_argument("sha256 received null data with nonzero size");
  }
  Sha256 hash;
  hash.update(data, size);
  return hash.finish();
}

Sha256Digest sha256(std::string_view text) {
  return sha256(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

Sha256Digest derive_seed(std::string_view domain,
                         std::initializer_list<std::uint64_t> values) {
  Sha256 hash;
  hash.update(reinterpret_cast<const std::uint8_t*>(domain.data()),
              domain.size());
  for (const std::uint64_t value : values) {
    update_little_endian_u64(hash, value);
  }
  return hash.finish();
}

Xoshiro256PlusPlus::Xoshiro256PlusPlus(const Sha256Digest& seed) noexcept
    : state_{} {
  for (std::size_t index = 0; index < state_.size(); ++index) {
    state_[index] = load_little_endian_u64(seed.data() + index * 8U);
  }
  if (std::all_of(state_.begin(), state_.end(),
                  [](std::uint64_t value) { return value == 0U; })) {
    state_[0] = 1U;
  }
}

Xoshiro256PlusPlus::Xoshiro256PlusPlus(
    std::array<std::uint64_t, 4> state) noexcept
    : state_(state) {
  if (std::all_of(state_.begin(), state_.end(),
                  [](std::uint64_t value) { return value == 0U; })) {
    state_[0] = 1U;
  }
}

std::uint64_t Xoshiro256PlusPlus::next() noexcept {
  const std::uint64_t result =
      rotate_left(state_[0] + state_[3], 23U) + state_[0];
  const std::uint64_t temporary = state_[1] << 17U;

  state_[2] ^= state_[0];
  state_[3] ^= state_[1];
  state_[1] ^= state_[2];
  state_[0] ^= state_[3];
  state_[2] ^= temporary;
  state_[3] = rotate_left(state_[3], 45U);

  return result;
}

std::uint64_t Xoshiro256PlusPlus::bounded(std::uint64_t bound) {
  if (bound == 0U) {
    throw std::invalid_argument("random bound must be nonzero");
  }

  const std::uint64_t threshold = (0U - bound) % bound;
  for (;;) {
    const std::uint64_t value = next();
    if (value >= threshold) {
      return value % bound;
    }
  }
}

const std::array<std::uint64_t, 4>& Xoshiro256PlusPlus::state() const noexcept {
  return state_;
}

Sha256Digest deal_seed(std::uint64_t match_seed,
                        std::uint64_t deal_index) {
  return derive_seed("felt/deal/v1", {match_seed, deal_index});
}

Deck shuffled_deck(std::uint64_t match_seed, std::uint64_t deal_index) {
  Deck deck{};
  for (std::uint8_t card = 0; card < kCardCount; ++card) {
    deck[card] = card;
  }

  Xoshiro256PlusPlus random(deal_seed(match_seed, deal_index));
  for (std::size_t index = deck.size() - 1U; index > 0U; --index) {
    const std::size_t other =
        static_cast<std::size_t>(random.bounded(index + 1U));
    std::swap(deck[index], deck[other]);
  }
  return deck;
}

std::uint64_t decision_random(std::uint64_t match_seed,
                              std::uint64_t randomness_index,
                              std::uint64_t decision_index,
                              std::uint32_t acting_position) {
  const Sha256Digest digest =
      derive_seed("felt/decision-random/v1",
                  {match_seed, randomness_index, decision_index,
                   static_cast<std::uint64_t>(acting_position)});
  return load_little_endian_u64(digest.data());
}

}  // namespace felt

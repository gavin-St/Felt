#include "felt/card.hpp"
#include "felt/random.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string hex(const felt::Sha256Digest& digest) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(digest.size() * 2U);
  for (const std::uint8_t byte : digest) {
    result.push_back(digits[byte >> 4U]);
    result.push_back(digits[byte & 0x0fU]);
  }
  return result;
}

template <typename Function>
void require_invalid_argument(Function function, const std::string& message) {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

}  // namespace

int main() {
  try {
    for (std::uint8_t card = 0; card < felt::kCardCount; ++card) {
      const std::string text = felt::card_to_string(card);
      require(felt::card_from_string(text) == card,
              "card string round trip failed for " + text);
    }
    require(felt::card_from_string("aH") == 51U,
            "case-insensitive card parsing failed");
    require(felt::card_to_string(0) == "2c", "card zero was not 2c");
    require(felt::card_to_string(51) == "Ah", "card 51 was not Ah");
    require_invalid_argument([] { (void)felt::card_from_string("1c"); },
                             "invalid rank was accepted");
    require_invalid_argument([] { (void)felt::card_from_string("Ace"); },
                             "long card string was accepted");
    require_invalid_argument([] { (void)felt::card_to_string(52); },
                             "invalid card value was accepted");

    require(hex(felt::sha256("")) ==
                "e3b0c44298fc1c149afbf4c8996fb924"
                "27ae41e4649b934ca495991b7852b855",
            "empty SHA-256 vector failed");
    require(hex(felt::sha256("abc")) ==
                "ba7816bf8f01cfea414140de5dae2223"
                "b00361a396177a9cb410ff61f20015ad",
            "abc SHA-256 vector failed");
    require(hex(felt::sha256(
                    "abcdbcdecdefdefgefghfghighijhijk"
                    "ijkljklmklmnlmnomnopnopq")) ==
                "248d6a61d20638b8e5c026930c3e6039"
                "a33ce45964ff2167f6ecedd419db06c1",
            "multi-block SHA-256 vector failed");

    const felt::Sha256Digest seed = felt::deal_seed(123U, 0U);
    require(hex(seed) ==
                "a0c1fdebdad4e0fefaf2763c2f8a860a"
                "9494ca0918ec6beb9203541c03ad6d42",
            "deal seed golden value changed");

    felt::Xoshiro256PlusPlus random(seed);
    const std::array<std::uint64_t, 4> expected_state{
        UINT64_C(0xfee0d4daebfdc1a0), UINT64_C(0x0a868a2f3c76f2fa),
        UINT64_C(0xeb6bec1809ca9494), UINT64_C(0x426dad031c540392)};
    require(random.state() == expected_state, "xoshiro seed words changed");

    const std::array<std::uint64_t, 5> expected_outputs{
        UINT64_C(0xede4fdbd851e68e0), UINT64_C(0x4024565131e94d46),
        UINT64_C(0x0c6fc49b9cb8e54a), UINT64_C(0x315fbe79f883dd47),
        UINT64_C(0x17d7b0ff7c3e20dc)};
    for (const std::uint64_t expected : expected_outputs) {
      require(random.next() == expected, "xoshiro golden output changed");
    }

    const felt::Deck expected_deck{
        4,  9,  45, 50, 3,  18, 22, 48, 43, 17, 2,  28, 31,
        36, 30, 34, 5,  13, 24, 39, 19, 8,  0,  15, 25, 49,
        40, 14, 41, 1,  26, 23, 37, 6,  21, 46, 20, 38, 35,
        27, 11, 29, 47, 51, 32, 7,  10, 12, 44, 42, 33, 16};
    const felt::Deck deck = felt::shuffled_deck(123U, 0U);
    require(deck == expected_deck, "deck golden value changed");

    felt::Deck sorted = deck;
    std::sort(sorted.begin(), sorted.end());
    for (std::uint8_t card = 0; card < felt::kCardCount; ++card) {
      require(sorted[card] == card, "shuffled deck was not a permutation");
    }
    require(felt::shuffled_deck(123U, 0U) == deck,
            "same deal seed did not reproduce");
    require(felt::shuffled_deck(123U, 1U) != deck,
            "different deal index produced the same deck");

    require(felt::decision_random(123U, 0U, 0U, FELT_POSITION_BUTTON) ==
                UINT64_C(0xe95acabd864d35af),
            "decision randomness golden value changed");
    require(felt::decision_random(123U, 0U, 1U, FELT_POSITION_BUTTON) !=
                felt::decision_random(123U, 0U, 0U, FELT_POSITION_BUTTON),
            "decision index did not alter decision randomness");

    require_invalid_argument(
        [&random] { (void)random.bounded(0U); },
        "zero random bound was accepted");
    require_invalid_argument(
        [] { (void)felt::sha256(nullptr, 1U); },
        "null SHA-256 input was accepted");
  } catch (const std::exception& error) {
    std::cerr << "card_random_test: " << error.what() << '\n';
    return 1;
  }

  return 0;
}

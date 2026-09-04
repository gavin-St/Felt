#include "felt/card.hpp"

#include <array>
#include <cctype>
#include <stdexcept>

namespace felt {
namespace {

constexpr std::array<char, kRankCount> kRankChars{
    '2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K', 'A'};
constexpr std::array<char, kSuitCount> kSuitChars{'c', 'd', 's', 'h'};

char uppercase(char value) {
  return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
}

char lowercase(char value) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

}  // namespace

std::string card_to_string(Card card) {
  if (!is_valid_card(card)) {
    throw std::invalid_argument("invalid card value");
  }

  return {kRankChars[card_rank(card)], kSuitChars[card_suit(card)]};
}

Card card_from_string(std::string_view text) {
  if (text.size() != 2) {
    throw std::invalid_argument("card text must contain exactly two characters");
  }

  const char rank_char = uppercase(text[0]);
  const char suit_char = lowercase(text[1]);

  std::uint8_t rank = kRankCount;
  for (std::uint8_t index = 0; index < kRankCount; ++index) {
    if (kRankChars[index] == rank_char) {
      rank = index;
      break;
    }
  }

  std::uint8_t suit = kSuitCount;
  for (std::uint8_t index = 0; index < kSuitCount; ++index) {
    if (kSuitChars[index] == suit_char) {
      suit = index;
      break;
    }
  }

  if (rank == kRankCount || suit == kSuitCount) {
    throw std::invalid_argument("invalid card text");
  }

  return static_cast<Card>((rank << 2U) | suit);
}

}  // namespace felt

#include "range_read.h"

#include <stddef.h>

/* What a line claims, before anything else is taken into account. */
#define CLAIM_NO_ACTION_YET 28
#define CLAIM_CHECKED 20
#define CLAIM_CALLED 34
#define CLAIM_BET 46
#define CLAIM_RAISED 68
#define CLAIM_RERAISED 82

static bool is_aggressive(uint32_t type) {
  return type == FELT_EVENT_BET || type == FELT_EVENT_RAISE;
}

/* How much the opponent's bet is, against the pot as it stood before it. A
 * big bet claims more than a small one, and this is the only place the size
 * of their bet is read at all. */
static int size_claim_adjustment(const FeltGameState* state) {
  if (state->to_call <= 0) {
    return 0;
  }
  const FeltChips before = state->pot - state->to_call;
  if (before <= 0) {
    return 0;
  }
  const int percent = (int)((100 * state->to_call) / before);
  /* Two thirds of the pot is the reference size and moves nothing. */
  int adjustment = (percent - 66) / 7;
  if (adjustment > 10) adjustment = 10;
  if (adjustment < -10) adjustment = -10;
  return adjustment;
}

/*
 * How much narrower each extra preflop raise makes a range. This is the
 * steepest thing in the read, and it should be: an opening range is most of a
 * deck, a three-bet is a tenth of it, a four-bet is the top few percent, and a
 * five-bet is aces and kings. Those are not neighbouring strengths, so the
 * ladder cannot be evenly spaced.
 */
static int preflop_pot_adjustment(uint32_t preflop_raises) {
  switch (preflop_raises) {
    case 0U:
      return -10; /* limped: any two cards */
    case 1U:
      return 0; /* a single open is the reference */
    case 2U:
      return 14; /* three-bet */
    case 3U:
      return 26; /* four-bet */
    default:
      return 34; /* five-bet and beyond */
  }
}

/*
 * Range advantage: who the board belongs to, before anyone has done anything
 * on it. It only means something once someone has raised before the flop,
 * because that is what separates the two ranges -- the raiser holds the aces,
 * kings and broadway cards, and the caller, by declining to raise, mostly does
 * not. So a king-high flop is the raiser's board and a seven-high one is the
 * caller's, and the score moves toward whichever of them the opponent is.
 *
 * Ranks run 0 for a deuce to 12 for an ace: a queen is 10, a nine is 7.
 */
static int range_advantage(uint32_t preflop_raises,
                           bool opponent_was_aggressor,
                           const FeltBoardTexture* texture) {
  if (preflop_raises == 0U || texture == NULL || !texture->valid) {
    return 0;
  }
  int advantage = 0;
  if (texture->high_rank >= 10U) {
    /* Queen high or better, and more so with a second broadway card. */
    advantage = texture->broadway_count >= 2U ? 9 : 6;
  } else if (texture->high_rank <= 7U) {
    /* Nine high or lower misses a raising range and finds the small pairs
     * and connectors a calling range keeps. */
    advantage = texture->max_cards_in_five_rank_window >= 3U ? -9 : -7;
  }
  /* Past a three-bet both ranges are made of big cards, so a low board no
   * longer belongs to the caller the way it does in a single-raised pot: the
   * four-betting range still holds the overpairs. Halve the swing. */
  if (preflop_raises >= 3U) {
    advantage /= 2;
  }
  return opponent_was_aggressor ? advantage : -advantage;
}

/*
 * Polarisation, read off the same evidence as the strength score but asking a
 * different question. Bet sizing is the loudest signal: nobody bets the pot
 * with a medium hand, and nobody bets a fifth of it with the nuts or with air.
 * The board matters too, in a way that is not about how scary it is. A board
 * where the obvious draw has arrived, and a board so dry that almost nothing
 * connects with it, both split a betting range in two -- they have it or they
 * are representing it. A wet, connected, undrawn board does the opposite: it
 * is full of pairs and draws worth betting, so the range merges.
 */
static int polarisation_of(const FeltGameState* state,
                           const FeltBoardTexture* texture,
                           uint32_t their_aggression,
                           uint32_t preflop_raises,
                           bool they_raised_us) {
  int score = 50;

  if (state->to_call > 0) {
    const FeltChips before = state->pot - state->to_call;
    if (before > 0) {
      const int percent = (int)((100 * state->to_call) / before);
      int size_term = (percent - 66) * 2 / 5;
      if (size_term > 20) size_term = 20;
      if (size_term < -20) size_term = -20;
      score += size_term;
    }
  } else if (their_aggression == 0U) {
    /* They checked or called. A passive range is capped, not polarised. */
    score -= 15;
  }

  /* A raise is a narrower, more two-sided action than a bet. */
  if (they_raised_us) score += 10;
  if (their_aggression >= 2U) score += 8;

  if (texture != NULL && texture->valid && their_aggression > 0U) {
    const bool draw_arrived =
        texture->max_suit_count >= 4U ||
        texture->max_cards_in_five_rank_window >= 4U;
    const bool very_dry = texture->max_suit_count <= 1U &&
                          texture->max_cards_in_five_rank_window <= 2U &&
                          texture->pair_count == 0U;
    const bool wet_and_live = !draw_arrived && texture->max_suit_count >= 2U &&
                              texture->max_cards_in_five_rank_window >= 3U;
    if (draw_arrived) {
      score += 12;
    } else if (very_dry) {
      score += 10;
    } else if (wet_and_live) {
      score -= 8;
    }
    /* A paired board gives a betting range fewer medium hands to protect. */
    if (texture->pair_count >= 1U) score += 5;
  }

  /* Four-betting ranges are uniformly strong, so they are merged rather than
   * split; limped ranges bet only their best and their worst. */
  if (preflop_raises >= 3U) {
    score -= 12;
  } else if (preflop_raises == 0U) {
    score += 6;
  }

  if (score < 0) score = 0;
  if (score > 100) score = 100;
  return score;
}

FeltRangeRead felt_read_range(const FeltGameState* state,
                              const FeltBoardTexture* texture) {
  FeltRangeRead read = {0};
  if (state == NULL || state->history == NULL) {
    return read;
  }
  read.valid = true;

  uint32_t their_aggression = 0;
  uint32_t their_calls = 0;
  uint32_t their_checks = 0;
  bool aggressor_seen = false;
  bool aggressor_is_theirs = false;

  for (uint32_t index = 0; index < state->history_count; index++) {
    const FeltActionEvent* event = &state->history[index];
    const bool theirs = event->position != state->position;

    if (event->street == FELT_STREET_PREFLOP && is_aggressive(event->type)) {
      read.preflop_raises++;
      aggressor_seen = true;
      aggressor_is_theirs = theirs;
    }
    if (event->street != state->street || !theirs) {
      continue;
    }
    if (is_aggressive(event->type)) {
      their_aggression++;
    } else if (event->type == FELT_EVENT_CALL) {
      their_calls++;
    } else if (event->type == FELT_EVENT_CHECK) {
      their_checks++;
    }
  }

  read.street_aggression = their_aggression;
  read.opponent_was_preflop_aggressor = aggressor_seen && aggressor_is_theirs;

  int score;
  if (their_aggression >= 3U) {
    score = CLAIM_RERAISED;
  } else if (their_aggression == 2U) {
    score = CLAIM_RAISED + 8;
  } else if (their_aggression == 1U) {
    /* One aggressive action is a bet if we have not acted, a raise if we
     * have -- and a raise of our bet is the stronger claim by far. */
    score = state->my_street_contribution > 0 ? CLAIM_RAISED : CLAIM_BET;
  } else if (their_calls > 0U) {
    score = CLAIM_CALLED;
  } else if (their_checks > 0U) {
    score = CLAIM_CHECKED;
  } else {
    score = CLAIM_NO_ACTION_YET;
  }

  score += size_claim_adjustment(state);

  score += preflop_pot_adjustment(read.preflop_raises);
  if (read.opponent_was_preflop_aggressor) {
    score += 4;
  }

  read.range_advantage = range_advantage(
      read.preflop_raises, read.opponent_was_preflop_aggressor, texture);
  score += read.range_advantage;

  if (score < 0) score = 0;
  if (score > 100) score = 100;
  read.score = score;

  read.polarisation =
      polarisation_of(state, texture, their_aggression, read.preflop_raises,
                      their_aggression > 0U && state->my_street_contribution > 0);
  return read;
}

int felt_geometric_bet_percent(FeltChips pot,
                               FeltChips effective_stack,
                               uint32_t street) {
  if (pot <= 0 || effective_stack <= 0 || street < FELT_STREET_FLOP ||
      street > FELT_STREET_RIVER) {
    return 0;
  }
  const int streets_left = (int)(FELT_STREET_RIVER - street) + 1;

  /* Want the largest f with pot * (100 + 2f)^n <= (pot + 2 * stack) * 100^n.
   * Both sides stay well inside 64 bits for any legal pot and stack. */
  int64_t scale = 1;
  for (int step = 0; step < streets_left; step++) {
    scale *= 100;
  }
  const int64_t target = (int64_t)(pot + 2 * effective_stack) * scale;

  /* Capped at one and a half times the pot: past that the geometric answer is
   * technically right and practically an announcement. */
  int low = 1;
  int high = 150;
  int best = 1;
  while (low <= high) {
    const int middle = (low + high) / 2;
    int64_t grown = pot;
    bool overflowed = false;
    for (int step = 0; step < streets_left; step++) {
      grown *= (100 + 2 * middle);
      if (grown > target) {
        overflowed = true;
        break;
      }
    }
    if (!overflowed && grown <= target) {
      best = middle;
      low = middle + 1;
    } else {
      high = middle - 1;
    }
  }
  return best;
}

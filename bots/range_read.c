#include "range_read.h"

#include <stddef.h>

/* What a line claims, before anything else is taken into account. */
#define CLAIM_NO_ACTION_YET 35
/* Acting first is not evidence that the opponent is weak. Their untouched
 * postflop range begins near neutral and is then moved by their actual
 * preflop line. */
#define CLAIM_UNOPENED_OUT_OF_POSITION 45
#define CLAIM_CHECKED 20
#define CLAIM_CALLED 34
/*
 * Six points come off every aggressive claim. A bet is evidence, but this read
 * is a prior about an opponent who bets for a reason, and it has no way to
 * learn that the one in front of it does not -- the harness forbids
 * remembering. Against a bot that bets everything it will still overfold, and
 * that is the honest cost of not being allowed to adapt; the discount keeps
 * that cost from swallowing the matches where the read is right.
 */
#define CLAIM_BET 40
#define CLAIM_RAISED 62
#define CLAIM_RERAISED 76
/*
 * The preflop raiser's own barrels, scored apart from a bet that had to be
 * decided on its own merits. A continuation bet is the widest bet in poker --
 * the whole raising range, hit or not -- and claims barely more than a check.
 * Each further barrel has given up on some of the hands that missed, so the
 * claim climbs, but never to what a bet from the player who did not raise
 * before the flop is worth.
 */
#define CLAIM_CONTINUATION_BET 22
#define CLAIM_SECOND_BARREL 34
#define CLAIM_THIRD_BARREL 42
/* An open of two and a half blinds or less is most of a deck whatever else
 * the ladder says, so preflop it replaces the ladder rather than adjusting
 * it. Postflop the pot is a single-raised pot like any other. */
#define SMALL_OPEN_SCORE 45
#define SMALL_OPEN_MAX_BB_X10 25

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

/* How much the opponent's own preflop action narrows their range. An opponent
 * who three-bet is not assigned the same range as one who called our
 * three-bet merely because both hands contained two raises. */
static int opponent_preflop_adjustment(FeltOpponentPreflopLine line) {
  switch (line) {
    case FELT_PREFLOP_LINE_NONE:
    case FELT_PREFLOP_LINE_LIMP:
      return -10;
    case FELT_PREFLOP_LINE_OPEN:
      return -6;
    case FELT_PREFLOP_LINE_CALL_OPEN:
      return 0;
    case FELT_PREFLOP_LINE_CALL_THREE_BET:
      return 10;
    case FELT_PREFLOP_LINE_THREE_BET:
      return 14;
    case FELT_PREFLOP_LINE_CALL_FOUR_BET_PLUS:
      return 22;
    default: /* the opponent four-bet or raised again */
      return 30;
  }
}

static FeltOpponentPreflopLine opponent_raise_line(uint32_t raises_before) {
  if (raises_before == 0U) return FELT_PREFLOP_LINE_OPEN;
  if (raises_before == 1U) return FELT_PREFLOP_LINE_THREE_BET;
  return FELT_PREFLOP_LINE_FOUR_BET_PLUS;
}

static FeltOpponentPreflopLine opponent_call_line(uint32_t raises_seen) {
  if (raises_seen == 0U) return FELT_PREFLOP_LINE_LIMP;
  if (raises_seen == 1U) return FELT_PREFLOP_LINE_CALL_OPEN;
  if (raises_seen == 2U) return FELT_PREFLOP_LINE_CALL_THREE_BET;
  return FELT_PREFLOP_LINE_CALL_FOUR_BET_PLUS;
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
                           bool they_raised_us,
                           bool they_check_raised) {
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

  /* A raise is a narrower, more two-sided action than a bet. A check-raise
   * is more polar still: it first declined to bet, then chose to inflate the
   * pot after seeing aggression. */
  if (they_raised_us) score += 10;
  if (they_check_raised) score += 12;
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

/*
 * How many streets this player has opened the betting on, counting the one
 * being decided. Opening is not the same as betting: a raise of someone
 * else's bet is a different action and is scored elsewhere.
 */
static uint32_t barrels_of(const FeltGameState* state, uint32_t position) {
  uint32_t count = 0;
  for (uint32_t street = FELT_STREET_FLOP; street <= state->street; street++) {
    bool opened = false;
    bool aggression_seen = false;
    for (uint32_t index = 0; index < state->history_count; index++) {
      const FeltActionEvent* event = &state->history[index];
      if (event->street != street || !is_aggressive(event->type)) continue;
      if (!aggression_seen && event->position == position) opened = true;
      aggression_seen = true;
    }
    if (opened) count++;
  }
  return count;
}

/* How often they have called our aggression this hand. Calling once is
 * ambiguous; calling three big bets is not, and nothing in the read noticed
 * the difference before. */
static uint32_t calls_of_our_bets(const FeltGameState* state) {
  uint32_t count = 0;
  uint32_t street = FELT_STREET_PREFLOP;
  bool ours_is_the_bet = false;
  for (uint32_t index = 0; index < state->history_count; index++) {
    const FeltActionEvent* event = &state->history[index];
    if (event->street != street) {
      street = event->street;
      ours_is_the_bet = false;
    }
    if (is_aggressive(event->type)) {
      ours_is_the_bet = event->position == state->position;
    } else if (event->type == FELT_EVENT_CALL &&
               event->position != state->position && ours_is_the_bet) {
      count++;
    }
  }
  return count;
}

/* Their preflop open, in tenths of a big blind, or zero if they did not open
 * or the blinds are not in the history. */
static int preflop_open_bb_x10(const FeltGameState* state) {
  FeltChips big_blind = 0;
  for (uint32_t index = 0; index < state->history_count; index++) {
    if (state->history[index].type == FELT_EVENT_POST_BIG_BLIND) {
      big_blind = state->history[index].amount_to;
      break;
    }
  }
  if (big_blind <= 0) return 0;
  for (uint32_t index = 0; index < state->history_count; index++) {
    const FeltActionEvent* event = &state->history[index];
    if (event->street != FELT_STREET_PREFLOP || !is_aggressive(event->type)) {
      continue;
    }
    if (event->position == state->position) return 0;
    return (int)((10 * event->amount_to) / big_blind);
  }
  return 0;
}

uint32_t felt_own_barrels(const FeltGameState* state) {
  if (state == NULL) return 0U;
  return barrels_of(state, state->position);
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
  bool they_checked_this_street = false;
  bool they_check_raised = false;
  bool aggressor_seen = false;
  bool aggressor_is_theirs = false;
  read.hero_out_of_position =
      state->street != FELT_STREET_PREFLOP &&
      state->position == FELT_POSITION_BIG_BLIND;

  for (uint32_t index = 0; index < state->history_count; index++) {
    const FeltActionEvent* event = &state->history[index];
    const bool theirs = event->position != state->position;

    if (event->street == FELT_STREET_PREFLOP) {
      if (is_aggressive(event->type)) {
        if (theirs) {
          read.opponent_preflop_line =
              opponent_raise_line(read.preflop_raises);
        }
        read.preflop_raises++;
        aggressor_seen = true;
        aggressor_is_theirs = theirs;
      } else if (theirs && event->type == FELT_EVENT_CALL) {
        read.opponent_preflop_line =
            opponent_call_line(read.preflop_raises);
      }
    }
    if (event->street != state->street || !theirs) {
      continue;
    }
    read.opponent_acted_this_street = true;
    if (is_aggressive(event->type)) {
      if (they_checked_this_street) they_check_raised = true;
      their_aggression++;
    } else if (event->type == FELT_EVENT_CALL) {
      their_calls++;
    } else if (event->type == FELT_EVENT_CHECK) {
      their_checks++;
      they_checked_this_street = true;
      read.opponent_checked_this_street = true;
    }
  }

  read.street_aggression = their_aggression;
  read.opponent_was_preflop_aggressor = aggressor_seen && aggressor_is_theirs;

  read.their_barrels = barrels_of(state, 1U - state->position);
  read.calls_of_our_bets = calls_of_our_bets(state);

  int score;
  if (their_aggression >= 3U) {
    score = CLAIM_RERAISED;
  } else if (their_aggression == 2U) {
    score = CLAIM_RAISED + 8;
  } else if (their_aggression == 1U) {
    /* One aggressive action is a bet if we have not acted, a raise if we
     * have -- and a raise of our bet is the stronger claim by far. */
    if (state->my_street_contribution > 0) {
      score = CLAIM_RAISED;
    } else if (aggressor_is_theirs && read.their_barrels > 0U) {
      /* Their own barrel, scored by how many streets they have fired. */
      score = read.their_barrels == 1U
                  ? CLAIM_CONTINUATION_BET
                  : (read.their_barrels == 2U ? CLAIM_SECOND_BARREL
                                              : CLAIM_THIRD_BARREL);
    } else {
      score = CLAIM_BET;
    }
  } else if (their_calls > 0U) {
    score = CLAIM_CALLED;
  } else if (their_checks > 0U) {
    score = CLAIM_CHECKED;
  } else {
    score = read.hero_out_of_position ? CLAIM_UNOPENED_OUT_OF_POSITION
                                      : CLAIM_NO_ACTION_YET;
  }

  score += size_claim_adjustment(state);
  /* Calling is not free of information once it has happened more than once. */
  score += 3 * (int)read.calls_of_our_bets;

  score += opponent_preflop_adjustment(read.opponent_preflop_line);
  if (read.opponent_was_preflop_aggressor) {
    score += 4;
  }

  read.range_advantage = range_advantage(
      read.preflop_raises, read.opponent_was_preflop_aggressor, texture);
  score += read.range_advantage;

  /* A min-raise open is most of a deck, and nothing else the ladder has to
   * say about a single-raised pot survives that. Preflop only: by the flop
   * the pot is a single-raised pot like any other. */
  if (state->street == FELT_STREET_PREFLOP && read.preflop_raises == 1U &&
      read.opponent_was_preflop_aggressor) {
    const int open = preflop_open_bb_x10(state);
    if (open > 0 && open <= SMALL_OPEN_MAX_BB_X10) score = SMALL_OPEN_SCORE;
  }

  if (score < 0) score = 0;
  if (score > 100) score = 100;
  read.score = score;

  read.polarisation =
      polarisation_of(state, texture, their_aggression, read.preflop_raises,
                      their_aggression > 0U && state->my_street_contribution > 0,
                      they_check_raised);
  /* Each call of ours narrows their range toward the middle of it. */
  read.polarisation -= 10 * (int)read.calls_of_our_bets;
  if (read.polarisation < 0) read.polarisation = 0;
  if (read.polarisation > 100) read.polarisation = 100;
  read.air_share_basis_points =
      read.polarisation * (100 - read.score);
  read.bluff_rate_basis_points =
      500 + (6000 * read.air_share_basis_points) / 10000;
  if (read.bluff_rate_basis_points < 500) read.bluff_rate_basis_points = 500;
  if (read.bluff_rate_basis_points > 4500) read.bluff_rate_basis_points = 4500;
  return read;
}

double felt_adjusted_hand_score(const FeltHandValue* value,
                                const FeltRangeRead* read) {
  if (value == NULL || read == NULL || !value->valid || !read->valid) {
    return 0.0;
  }
  return (double)value->points - 0.5 * ((double)read->score - 50.0);
}

double felt_range_delta(const FeltHandValue* value,
                        const FeltRangeRead* read) {
  return felt_adjusted_hand_score(value, read) - 50.0;
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

  /* No cap. Nothing bets this number any more -- it only leans the choice
   * between two sizes that were picked in advance -- so an answer past any
   * size a bot would use simply means "the larger one", which is right. */
  int low = 1;
  int high = 300;
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

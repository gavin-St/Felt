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

FeltRangeRead felt_read_range(const FeltGameState* state) {
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

  /* Preflop context. A three-bet pot is a stronger range on every street
   * after it; a limped pot is a weaker one, whatever happens later. */
  if (read.preflop_raises >= 2U) {
    score += 8;
  } else if (read.preflop_raises == 0U) {
    score -= 8;
  }
  if (read.opponent_was_preflop_aggressor) {
    score += 4;
  }

  if (score < 0) score = 0;
  if (score > 100) score = 100;
  read.score = score;
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

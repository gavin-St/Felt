/*
 * worse-all-in: shoves only bad hands, folds everything else.
 *
 * A deliberately terrible baseline. The range is the complement of everything
 * worth playing:
 *
 *   - no pocket pairs;
 *   - no suited hands;
 *   - no connectors or one-gappers (rank gap of at least 3);
 *   - no aces or kings.
 *
 * That leaves 432 of 1,326 combinations, 32.6% of hands, every one of them
 * offsuit, unpaired, disconnected and headed by a queen or worse -- Q9o down to
 * 72o. It shoves exactly the hands the other bots fold and folds the ones they
 * shove, so it should lose badly to anything willing to call it. Check-fold is
 * the deliberate exception: a bot that never calls can still be bluffed by
 * junk. This remains a useful floor and rating-tool sanity check.
 */

#include "felt/bot_api.h"

/* rank = card >> 2, with 0 = deuce ... 10 = Q, 11 = K, 12 = A. */
#define RANK_QUEEN 10U
#define MIN_GAP 3U

static uint32_t rank_of(FeltCard card) { return (uint32_t)(card >> 2U); }
static uint32_t suit_of(FeltCard card) { return (uint32_t)(card & 3U); }

static int is_junk(const FeltGameState* state) {
  const uint32_t first = rank_of(state->hole[0]);
  const uint32_t second = rank_of(state->hole[1]);
  const uint32_t high = first > second ? first : second;
  const uint32_t low = first > second ? second : first;

  if (first == second) {
    return 0; /* a pair is never junk */
  }
  if (suit_of(state->hole[0]) == suit_of(state->hole[1])) {
    return 0; /* suited hands play too well */
  }
  if (high > RANK_QUEEN) {
    return 0; /* an ace or a king is too strong */
  }
  return (high - low) >= MIN_GAP; /* disconnected enough to be junk */
}

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char* felt_bot_name(void) { return "worse-all-in"; }

FeltAction felt_bot_act(const FeltGameState* state) {
  FeltAction action = {0};

  if (is_junk(state)) {
    if ((state->legal_actions & FELT_LEGAL_RAISE_TO) != 0U) {
      action.type = FELT_ACTION_RAISE_TO;
      action.amount_to = state->max_raise_to;
      return action;
    }
    if ((state->legal_actions & FELT_LEGAL_CALL) != 0U) {
      action.type = FELT_ACTION_CALL;
      return action;
    }
  }

  action.type = (state->legal_actions & FELT_LEGAL_FOLD) != 0U
                    ? FELT_ACTION_FOLD
                    : FELT_ACTION_CHECK;
  return action;
}

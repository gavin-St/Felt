#include "bet_sizing.h"

#include <stddef.h>

/* Thin value stays small either way -- it is a bet that wants a worse hand to
 * call, and no worse hand calls a big one -- so its pair is the two small
 * sizes, evenly split before anything else weighs in. */
static void base_pair(FeltSizingIntent intent,
                      bool facing_raise,
                      bool polarised,
                      double* small,
                      double* large,
                      int* weight_large) {
  if (intent == FELT_SIZING_THIN_VALUE) {
    *small = 0.33; *large = 0.5; *weight_large = 50;
    return;
  }
  if (!facing_raise) {
    if (polarised) {
      if (intent == FELT_SIZING_BLUFF) {
        *small = 0.33; *large = 0.5; *weight_large = 50;
      } else {
        *small = 0.5; *large = 1.25; *weight_large = 75;
      }
    } else {
      if (intent == FELT_SIZING_BLUFF) {
        *small = 0.66; *large = 1.25; *weight_large = 33;
      } else {
        *small = 0.33; *large = 0.66; *weight_large = 66;
      }
    }
    return;
  }
  if (polarised) {
    if (intent == FELT_SIZING_BLUFF) {
      /* Three times the pot is also the merged value re-raise, so the biggest
       * of the two is not a hand class on its own. */
      *small = 2.0; *large = 3.0; *weight_large = 50;
    } else {
      *small = 2.0; *large = 4.0; *weight_large = 67;
    }
  } else {
    if (intent == FELT_SIZING_BLUFF) {
      *small = 2.5; *large = 4.0; *weight_large = 66;
    } else {
      *small = 2.5; *large = 3.0; *weight_large = 67;
    }
  }
}

FeltSizing felt_choose_size(const FeltGameState* state,
                            const FeltRangeRead* read,
                            const FeltBoardTexture* texture,
                            const FeltDraws* draws,
                            FeltSizingIntent intent,
                            bool facing_raise,
                            int geometric_percent) {
  FeltSizing sizing = {0};
  if (state == NULL || read == NULL) {
    sizing.fraction = 0.66;
    sizing.small = sizing.large = 0.66;
    return sizing;
  }

  const bool polarised = read->polarisation >= FELT_POLARISED_AT;
  base_pair(intent, facing_raise, polarised, &sizing.small, &sizing.large,
            &sizing.weight_large);

  int weight = sizing.weight_large;

  if (texture != NULL && texture->valid) {
    /* A board with live draws on it is worth charging for; one with nothing
     * to draw to is not, because there is no equity to deny. */
    const bool wet = texture->max_suit_count >= 3U ||
                     texture->max_cards_in_five_rank_window >= 4U;
    const bool dry = texture->max_suit_count <= 1U &&
                     texture->max_cards_in_five_rank_window <= 2U;
    if (wet) {
      weight += 12;
    } else if (dry) {
      weight -= 12;
    }
    /* Paired boards hand out fewer second-best hands to get paid by. */
    if (texture->pair_count >= 1U) weight -= 8;
  }

  if (intent == FELT_SIZING_BLUFF) {
    /* A bluff with outs can afford the bigger one: it still has a hand when
     * it is called. A bluff into a range that has shown nothing does not need
     * the bigger one, because the small size folds out the same hands. */
    if (draws != NULL && draws->valid && draws->improving_next_cards >= 8U) {
      weight += 10;
    }
    if (read->score < 35) weight -= 10;
  } else {
    /* The further ahead we are, the less the bigger size costs us. */
    if (read->score < 30) weight += 6;
  }

  /* Whichever candidate lands nearer the size that gets the stacks in gets
   * the nod -- the plan still gets a vote, it just no longer dictates. */
  if (geometric_percent > 0) {
    const double geometric = (double)geometric_percent / 100.0;
    const double to_small =
        geometric > sizing.small ? geometric - sizing.small : sizing.small - geometric;
    const double to_large =
        geometric > sizing.large ? geometric - sizing.large : sizing.large - geometric;
    weight += to_large < to_small ? 15 : -15;
  }

  if (weight < 5) weight = 5;
  if (weight > 95) weight = 95;
  sizing.weight_large = weight;

  /* Its own slice of the per-decision randomness, so the size a hand takes is
   * independent of whether it decided to bluff in the first place. */
  const uint64_t roll = (state->decision_random >> 32U) % UINT64_C(100);
  sizing.took_large = (int)roll < weight;
  sizing.fraction = sizing.took_large ? sizing.large : sizing.small;
  return sizing;
}

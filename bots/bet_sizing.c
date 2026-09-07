#include "bet_sizing.h"

#include <stddef.h>

static void base_pair(FeltSizingIntent intent,
                      bool facing_bet,
                      bool facing_raise,
                      bool polarised,
                      double* small,
                      double* large,
                      int* weight_large,
                      bool* relative_to_opponent) {
  *relative_to_opponent = facing_bet;
  if (intent == FELT_SIZING_THIN_VALUE) {
    *small = *large = polarised ? 0.33 : 0.5;
    *weight_large = 0;
    return;
  }
  if (!facing_bet) {
    if (polarised) {
      *small = 0.33; *large = 0.66; *weight_large = 30;
    } else {
      *small = 0.66; *large = 1.25; *weight_large = 60;
    }
    return;
  }
  if (facing_raise) {
    *small = 2.5; *large = 3.0; *weight_large = 50;
  } else if (polarised) {
    *small = 3.0; *large = 3.5; *weight_large = 30;
  } else {
    *small = 3.0; *large = 4.5; *weight_large = 60;
  }
}

static double planned_pot_fraction(const FeltGameState* state,
                                   double size,
                                   bool relative_to_opponent) {
  if (!relative_to_opponent) return size;
  const double after_call = (double)(state->pot + state->to_call);
  if (after_call <= 0.0) return size;
  const double raise_by =
      (double)state->opp_street_contribution * (size - 1.0);
  return raise_by > 0.0 ? raise_by / after_call : 0.0;
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
  const bool facing_bet = state->to_call > 0;
  base_pair(intent, facing_bet, facing_raise, polarised, &sizing.small,
            &sizing.large, &sizing.weight_large,
            &sizing.relative_to_opponent);

  const bool single_size = sizing.small == sizing.large;
  if (single_size) {
    sizing.fraction = sizing.small;
    sizing.took_large = false;
    return sizing;
  }

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

  if (draws != NULL && draws->valid && draws->improving_next_cards >= 8U) {
    weight += 10;
  }
  if (read->street_aggression == 0U) weight += 8;

  /* Whichever candidate lands nearer the size that gets the stacks in gets
   * the nod -- the plan still gets a vote, it just no longer dictates. */
  if (geometric_percent > 0) {
    const double geometric = (double)geometric_percent / 100.0;
    const double small_plan = planned_pot_fraction(
        state, sizing.small, sizing.relative_to_opponent);
    const double large_plan = planned_pot_fraction(
        state, sizing.large, sizing.relative_to_opponent);
    const double to_small = geometric > small_plan ? geometric - small_plan
                                                    : small_plan - geometric;
    const double to_large = geometric > large_plan ? geometric - large_plan
                                                    : large_plan - geometric;
    weight += to_large < to_small ? 15 : -15;
  }

  if (weight < 10) weight = 10;
  if (weight > 90) weight = 90;
  sizing.weight_large = weight;

  /* Its own slice of the per-decision randomness, so the size a hand takes is
   * independent of whether it decided to bluff in the first place. */
  const uint64_t roll = (state->decision_random >> 32U) % UINT64_C(100);
  sizing.took_large = (int)roll < weight;
  sizing.fraction = sizing.took_large ? sizing.large : sizing.small;
  return sizing;
}

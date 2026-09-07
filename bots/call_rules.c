#include "call_rules.h"

#include <stddef.h>

#define DELTA_CALL 6

static bool facing_raise(const FeltGameState* state) {
  return state->my_street_contribution > 0 && state->to_call > 0;
}

static double bet_fraction(const FeltGameState* state) {
  if (state->to_call <= 0) return 0.0;
  const FeltChips before = state->pot - state->to_call;
  if (before <= 0) return 1000.0;
  return (double)state->to_call / (double)before;
}

static int size_row(double fraction) {
  if (fraction <= 0.33) return 0;
  if (fraction <= 0.66) return 1;
  if (fraction <= 1.00) return 2;
  if (fraction <= 1.50) return 3;
  return 4;
}

int felt_bluff_catch_frequency(const FeltGameState* state,
                               double delta,
                               const FeltRangeRead* read) {
  if (state == NULL || read == NULL || !read->valid || state->to_call <= 0) {
    return 0;
  }
  static const int merged_near[5] = {100, 70, 45, 25, 10};
  static const int polar_near[5] = {100, 90, 70, 50, 35};
  static const int merged_thin[5] = {60, 35, 15, 5, 0};
  static const int polar_thin[5] = {80, 55, 35, 20, 10};

  const bool raised = facing_raise(state);
  const bool polarised = read->polarisation >= FELT_POLARISED_AT;
  const int shift = raised ? 10 : 0;
  const int row = size_row(bet_fraction(state));
  int frequency;
  if (delta >= (double)(-10 + shift) &&
      delta <= (double)(5 + shift)) {
    frequency = polarised ? polar_near[row] : merged_near[row];
  } else if (delta >= (double)(-25 + shift) &&
             delta <= (double)(-11 + shift)) {
    frequency = polarised ? polar_thin[row] : merged_thin[row];
  } else {
    return 0;
  }

  if (state->street == FELT_STREET_RIVER) frequency -= 10;
  if (raised) frequency -= 15;
  /* The table is neutral at P=R=50, where the estimate is 20%. Move its
   * frequency one point for every point the exact estimate differs. */
  frequency += (read->bluff_rate_basis_points - 2000) / 100;
  if (frequency < 0) frequency = 0;
  if (frequency > 100) frequency = 100;
  return frequency;
}

static bool draw_is_priced(const FeltGameState* state,
                           const FeltHandValue* value) {
  if (state->street == FELT_STREET_RIVER || value->draw_class == FELT_DRAW_CLASS_NONE ||
      value->draw_class == FELT_DRAW_CLASS_BACKDOOR) {
    return false;
  }
  double cap = 0.0;
  switch (value->draw_class) {
    case FELT_DRAW_CLASS_COMBO:
      cap = state->street == FELT_STREET_FLOP ? 1.00 : 0.75;
      break;
    case FELT_DRAW_CLASS_FLUSH:
    case FELT_DRAW_CLASS_OPEN_ENDED:
      cap = state->street == FELT_STREET_FLOP ? 0.60 : 0.40;
      break;
    case FELT_DRAW_CLASS_GUTSHOT:
      cap = state->street == FELT_STREET_FLOP ? 0.25 : 0.15;
      break;
    default:
      return false;
  }
  return bet_fraction(state) <= cap;
}

bool felt_should_call(const FeltGameState* state,
                      const FeltHandValue* value,
                      const FeltRangeRead* read,
                      const FeltDraws* draws) {
  if (state == NULL || value == NULL || read == NULL || !value->valid) {
    return false;
  }
  if (state->to_call <= 0) {
    return true; /* checking is free */
  }

  const bool raised = facing_raise(state);
  const double delta = felt_range_delta(value, read);
  if (delta >= (double)(DELTA_CALL + (raised ? 10 : 0))) {
    return true;
  }
  if (draws != NULL && draws->valid && draws->improving_next_cards > 0U &&
      draw_is_priced(state, value)) {
    return true;
  }
  const int frequency = felt_bluff_catch_frequency(state, delta, read);
  return (int)((state->decision_random >> 8U) % UINT64_C(100)) < frequency;
}

#include "call_rules.h"

#include <stddef.h>

/* Enough of an edge to call a bet with, without needing a price. */
#define EDGE_CALL 6
/* Below this the hand is not ahead of anything and is folding on merit. */
#define EDGE_BLUFF_CATCH (-14)
/* A hand that cannot win a showdown cannot bluff-catch with one. */
#define SHOWDOWN_VALUE_POINTS 14

int felt_bluff_catch_ceiling(int range_score) {
  /* The weaker their range looks, the more of the pot it is worth paying to
   * find out. A range that has only checked can be called at nearly half the
   * pot; one that has raised twice, barely a tenth. */
  int ceiling = 42 - range_score / 3;
  if (ceiling < 12) ceiling = 12;
  if (ceiling > 45) ceiling = 45;
  return ceiling;
}

bool felt_should_call(const FeltGameState* state,
                      const FeltHandValue* value,
                      const FeltRangeRead* read,
                      const FeltDraws* draws) {
  if (state == NULL || value == NULL || read == NULL || !value->valid) {
    return false;
  }
  if (state->to_call <= 0) {
    /* Checking is free, and a hand worth showing down takes it. */
    return value->points >= SHOWDOWN_VALUE_POINTS;
  }

  const int edge = value->points - read->score;
  if (edge >= EDGE_CALL) {
    return true;
  }
  /* A draw is priced on its own arithmetic, whoever it is up against. */
  if (draws != NULL && draws->improving_next_cards > 0 &&
      felt_draw_price_is_right(state, draws)) {
    return true;
  }
  return value->points >= SHOWDOWN_VALUE_POINTS && edge >= EDGE_BLUFF_CATCH &&
         felt_call_price_percent(state) <=
             felt_bluff_catch_ceiling(read->score);
}

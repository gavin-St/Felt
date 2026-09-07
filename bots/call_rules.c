#include "call_rules.h"

#include <stddef.h>

/* Enough of an edge to call a bet with, without needing a price. */
#define EDGE_CALL 6
/*
 * How far behind the opponent's apparent range a hand can be and still be
 * worth looking someone up with. This is the widest band in the bot and it
 * needs to be: the read is a prior about a rational opponent, and against one
 * that bets everything the claim is inflated by the whole width of its
 * bluffing range. At -14 the crusher folded to anything that raised, which
 * against the bots that raise with nothing is simply paying them.
 */
#define EDGE_BLUFF_CATCH (-25)
/* A hand that cannot win a showdown cannot bluff-catch with one. */
#define SHOWDOWN_VALUE_POINTS 14

int felt_bluff_catch_ceiling(int range_score) {
  /* The weaker their range looks, the more of the pot it is worth paying to
   * find out. The old ceiling stopped at 42 minus a third, which came to 20%
   * against a range that had raised -- and a raise of our own bet costs about
   * 27% of the pot it builds, so the bluff-catch could never fire against a
   * raise at all. It has to clear that number to mean anything. */
  int ceiling = 50 - range_score / 3;
  if (ceiling < 16) ceiling = 16;
  if (ceiling > 50) ceiling = 50;
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

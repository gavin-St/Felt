#ifndef FELT_RANGE_READ_H
#define FELT_RANGE_READ_H

#include "felt/bot_api.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Two things board_value.c cannot do, because both need the action history.
 *
 * The first is a score for the opponent's range on the same nought-to-a-
 * hundred scale a hand is scored on, read from what they have actually done:
 * a range that has only checked is a weak one, a range that has raised twice
 * is not. Putting both on one scale means the whole decision becomes a
 * subtraction -- our hand, less what their line claims -- and that one number
 * makes the bot value bet thinner against weakness and fold more against
 * strength without a rule for either.
 *
 * The second is a bet size that plans. A fixed fraction of the pot cannot get
 * two hundred big blinds in by the river; the geometric size can, by asking
 * what fraction, repeated once a street, ends exactly all in.
 */

typedef struct FeltRangeRead {
  bool valid;
  /* 0 to 100, comparable with FeltHandValue.points. */
  int score;
  /* Bets and raises the opponent has made on the current street. */
  uint32_t street_aggression;
  /* Voluntary preflop raises by anyone: 0 limped, 1 opened, 2 three-bet. */
  uint32_t preflop_raises;
  bool opponent_was_preflop_aggressor;
} FeltRangeRead;

FeltRangeRead felt_read_range(const FeltGameState* state);

/*
 * The bet, as a percentage of the pot, that gets the effective stack in by the
 * river if it is repeated on every street left and called every time. Solves
 * (1 + 2f)^n = 1 + 2 * SPR in integers, so it cannot drift between platforms
 * and needs no libm. Returns 0 when there is nothing to size.
 */
int felt_geometric_bet_percent(FeltChips pot,
                               FeltChips effective_stack,
                               uint32_t street);

#ifdef __cplusplus
}
#endif

#endif

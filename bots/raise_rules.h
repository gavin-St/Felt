#ifndef FELT_RAISE_RULES_H
#define FELT_RAISE_RULES_H

#include "felt/bot_api.h"

#include "bet_sizing.h"
#include "board_value.h"
#include "range_read.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * When to put chips in, and whether the reason is value or a bluff. Nothing
 * here decides how many chips -- that is bet_sizing.c, and keeping the two
 * apart is what lets the same decision take two different sizes.
 *
 * Everything turns on one number: our hand's points on this board, less what
 * the opponent's line claims on the same scale. A rule about a hand class
 * would have to be written twice, once for a weak range and once for a strong
 * one; a rule about the edge is written once.
 */

typedef struct FeltRaisePlan {
  bool raise;
  FeltSizingIntent intent; /* value or thin value; bluffs come separately */
} FeltRaisePlan;

/* Value and thin value only. Asked first, before any calling. */
FeltRaisePlan felt_value_raise(const FeltGameState* state,
                               const FeltHandValue* value,
                               const FeltRangeRead* read);

/*
 * Whether to bluff, asked last -- only of hands that were going to fold
 * anyway. The frequency comes off the opponent's strength rather than the
 * edge, because how often to bluff is not the same question as how far ahead
 * we are: a range that has shown nothing gets bluffed at one time in three, a
 * range that has raised twice one time in eight.
 */
bool felt_bluff_raise(const FeltGameState* state,
                      const FeltHandValue* value,
                      const FeltRangeRead* read);

#ifdef __cplusplus
}
#endif

#endif

#ifndef FELT_BOARD_VALUE_H
#define FELT_BOARD_VALUE_H

#include "felt/bot_api.h"
#include "felt/bot_kit.h"

#include <stdbool.h>

/*
 * Two pieces of arithmetic the street-local family never does: what a hand is
 * worth *relative to the board it is on*, and whether a draw is being offered
 * a price worth taking. Both read only the current street, so a policy built
 * on them is still street-local.
 */

typedef enum FeltHandBand {
  FELT_BAND_AIR = 0,
  FELT_BAND_MARGINAL = 1,
  FELT_BAND_MEDIUM = 2,
  FELT_BAND_STRONG = 3,
  FELT_BAND_NUTTED = 4
} FeltHandBand;

typedef struct FeltHandValue {
  bool valid;
  /* 0 to 100. Absolute hand class, less what the board threatens. */
  int points;
  FeltHandBand band;
} FeltHandValue;

FeltHandValue felt_board_relative_value(const FeltMadeHand* made,
                                        const FeltBoardTexture* texture);

/* Share of the pot-after-calling that the call itself costs, in percent.
 * Returns 0 when nothing is owed. All integer, so it cannot drift. */
int felt_call_price_percent(const FeltGameState* state);

/* The rule of four and two: four percent an out on the flop, two on the turn. */
int felt_draw_equity_percent(const FeltGameState* state,
                             const FeltDraws* draws);

/* True when the draw's equity covers the price being asked. */
bool felt_draw_price_is_right(const FeltGameState* state,
                              const FeltDraws* draws);

#endif

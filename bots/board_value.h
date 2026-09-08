#ifndef FELT_BOARD_VALUE_H
#define FELT_BOARD_VALUE_H

#include "felt/bot_api.h"
#include "felt/bot_kit.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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

typedef enum FeltKickerBand {
  FELT_KICKER_NONE = 0,
  FELT_KICKER_WEAK = 1,
  FELT_KICKER_STRONG = 2,
  /* The board supplies the complete five-card hand; no kicker plays. */
  FELT_KICKER_PLAYS_BOARD = 3
} FeltKickerBand;

typedef enum FeltDrawClass {
  FELT_DRAW_CLASS_NONE = 0,
  FELT_DRAW_CLASS_BACKDOOR = 1,
  FELT_DRAW_CLASS_GUTSHOT = 2,
  FELT_DRAW_CLASS_OPEN_ENDED = 3,
  FELT_DRAW_CLASS_FLUSH = 4,
  FELT_DRAW_CLASS_COMBO = 5
} FeltDrawClass;

typedef struct FeltHandValue {
  bool valid;
  /* 0 to 100. The stronger of made-hand and draw value, less threats. */
  int points;
  int made_points;
  int draw_points;
  int board_penalty;
  FeltHandBand band;
  FeltKickerBand kicker;
  FeltDrawClass draw_class;
  bool plays_board;
  /* At least a pair that uses a hole card, or a stronger private hand. A
   * pair/two-pair/trips supplied entirely by the board does not count. */
  bool player_made_pair_or_better;
} FeltHandValue;

FeltHandValue felt_board_relative_value(const FeltGameState* state,
                                        const FeltMadeHand* made,
                                        const FeltDraws* draws,
                                        const FeltBoardTexture* texture);

/* Banding is a separate step because the bar moves. A raise of our own bet is
 * a much stronger range than an opening bet, so facing one the two top bands
 * ask for more points; the lower two are unchanged, since they are already
 * only bluff-catchers and the price is what decides them. */
FeltHandBand felt_band_for_points(int points, bool facing_raise);

/* Share of the pot-after-calling that the call itself costs, in percent.
 * Returns 0 when nothing is owed. All integer, so it cannot drift. */
int felt_call_price_percent(const FeltGameState* state);

/*
 * How many ranks of the suit we are drawing to beat our own card, or zero
 * when we hold no flush draw. A three-high flush draw makes a flush that
 * loses to every other heart, and neither its value nor its outs should be
 * counted as though it were the nuts.
 */
int felt_flush_draw_rank_gap(const FeltGameState* state);

/*
 * What is left behind, as a percentage of the pot we would be raising into.
 *
 * A bluff raise needs somewhere to go. With a hundred big blinds behind a
 * ten-blind pot the threat is the rest of the hand; with fifty behind a
 * hundred-blind pot the raise is a shove, and a shove lays a price the
 * opponent takes with anything that can beat a bluff. The stack has to be
 * part of the decision, and it was not.
 */
int felt_stack_pot_percent(const FeltGameState* state);

/* The rule of four and two: four percent an out on the flop, two on the turn. */
int felt_draw_equity_percent(const FeltGameState* state,
                             const FeltDraws* draws);

/* True when the draw's equity covers the price being asked. */
bool felt_draw_price_is_right(const FeltGameState* state,
                              const FeltDraws* draws);

#ifdef __cplusplus
}
#endif

#endif

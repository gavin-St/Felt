#ifndef FELT_RANGE_READ_H
#define FELT_RANGE_READ_H

#include "felt/bot_api.h"
#include "felt/bot_kit.h"

#include "board_value.h"

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
  /* The board's own contribution, kept separate so it can be inspected. */
  int range_advantage;
  /*
   * How polarised their range is, 0 to 100, where 50 is neither. A polarised
   * range is nuts-or-nothing: it wants a big bet, and there is nothing in the
   * middle of it to punish. A merged one is full of medium hands, so it bets
   * smaller and calls wider. This is a different question from how strong the
   * range is, and it decides our sizing rather than our action.
   */
  int polarisation;
  /* P * (100 - R), expressed in basis points, and the resulting clamped
   * 5%-45% estimate from 5% + 0.6 * air_share. */
  int air_share_basis_points;
  int bluff_rate_basis_points;
  /* Streets they have opened the betting on, counting this one, and how often
   * they have called our aggression this hand. */
  uint32_t their_barrels;
  uint32_t calls_of_our_bets;
} FeltRangeRead;

/* The same count for us: how many streets we have opened the betting on,
 * counting this one. Betting again is a stronger claim than betting once, so
 * the bar for doing it rises with every barrel. */
uint32_t felt_own_barrels(const FeltGameState* state);

#define FELT_POLARISED_AT 55

FeltRangeRead felt_read_range(const FeltGameState* state,
                              const FeltBoardTexture* texture);

/*
 * What their claim is worth on top of itself, given how many streets are left
 * to be wrong on. A bet on the flop has to be survived twice more before the
 * money is decided; the same bet on the river has to be survived not at all,
 * and the pot odds are then the whole of the question. The read has no street
 * term of its own -- it scores the line, not the moment -- so continuing
 * against an early bet was being priced as though it ended the hand.
 *
 * It is charged to continuing, not to value raising: raising a hand that is
 * already ahead of their range is not made worse by there being cards to
 * come, but calling one that is barely ahead is.
 */
int felt_street_premium(const FeltGameState* state);

/* score - 0.5 * (R - 50), and the same value centred on 50. */
double felt_adjusted_hand_score(const FeltHandValue* value,
                                const FeltRangeRead* read);
double felt_range_delta(const FeltHandValue* value,
                        const FeltRangeRead* read);

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

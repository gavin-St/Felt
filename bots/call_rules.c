#include "call_rules.h"

#include <stddef.h>

#define DELTA_CALL 6
/* An opening bet has many more bluffs than a raise of our own bet, so its
 * outer bluff-catching band is allowed five more points of weakness. */
#define INITIAL_BET_THIN_CATCH (-30)
#define RAISE_THIN_CATCH (-25)
/* Facing a raise of our own bet, every threshold moves up by this much. */
#define RAISE_SHIFT 8
/* Below this, a hand cannot win a showdown and is not a bluff-catcher. */
#define PAIR_POINTS 17

static bool facing_raise(const FeltGameState* state) {
  return state->my_street_contribution > 0 && state->to_call > 0;
}

/* Their bet as a fraction of the pot it was made into, in hundredths. */
static int bet_fraction_percent(const FeltGameState* state) {
  if (state->to_call <= 0) return 0;
  const FeltChips before =
      state->pot - state->to_call - state->my_street_contribution;
  if (before <= 0) return 1000;
  return (int)((100 * state->to_call) / before);
}

/* Was our own bet, the one they raised, the large side of its pair? We cannot
 * ask the sizing module after the fact -- it would re-roll -- so this reads
 * the bet back off the pot. Anything at or above nine tenths of the pot it
 * was made into is the large candidate in every row that has one. */
static bool we_bet_large(const FeltGameState* state) {
  if (state->my_street_contribution <= 0) return false;
  const FeltChips before =
      state->pot - state->my_street_contribution -
      state->opp_street_contribution;
  if (before <= 0) return true;
  return 100 * state->my_street_contribution >= 90 * before;
}

static bool two_overcards(const FeltGameState* state) {
  if (state->board_count == 0U) return false;
  uint8_t highest = 0;
  for (uint8_t index = 0; index < state->board_count; index++) {
    const uint8_t rank = (uint8_t)(state->board[index] / 4U);
    if (rank > highest) highest = rank;
  }
  return (uint8_t)(state->hole[0] / 4U) > highest &&
         (uint8_t)(state->hole[1] / 4U) > highest;
}

/*
 * Live outs, in tenths of an out.
 *
 * Built from the kit's straight and flush counters rather than its
 * improvement counter: that one counts every card that improves the hand
 * class, so it reports fifteen for a bare flush draw and six for two
 * overcards on a dry board. The clean counters agree with the nominal
 * numbers -- nine, eight, four -- and the rest is added here.
 */
int felt_live_outs_x10(const FeltGameState* state,
                       const FeltHandValue* value,
                       const FeltDraws* draws) {
  if (draws == NULL || !draws->valid) return 0;
  /*
   * Flush outs are only outs if the flush wins. With three of the suit on the
   * board, a card that completes ours completes everybody's, and the one with
   * the higher card of that suit takes it -- so a low draw is worth fewer
   * outs than it counts. One out comes off for every three ranks above ours.
   */
  int flush_outs = (int)draws->flush_next_cards;
  if (flush_outs > 0) {
    const int discount = felt_flush_draw_rank_gap(state) / 3;
    flush_outs -= discount > 6 ? 6 : discount;
    if (flush_outs < 0) flush_outs = 0;
  }
  int outs = 10 * ((int)draws->straight_next_cards + flush_outs);
  /* A card can make both, and the two counters do not know about each other;
   * the nominal fifteen for a flush plus an open-ender is this correction. */
  if (draws->straight_next_cards > 0U && draws->flush_next_cards > 0U) {
    outs -= 20;
  }
  if (value != NULL && value->valid) {
    /* A pair alongside a draw also improves to two pair or trips. */
    if (outs > 0 && value->made_points >= PAIR_POINTS &&
        value->made_points < 54) {
      outs += 50;
    }
    if (value->draw_class == FELT_DRAW_CLASS_BACKDOOR) outs += 15;
    /* Two live overcards are worth three outs, and they stack with a
     * backdoor rather than replacing it. */
    if (value->made_points < PAIR_POINTS && two_overcards(state)) outs += 30;
  }

  /* An out that also fills the board's own draw is not an out. */
  const FeltBoardTexture texture =
      felt_board_texture(state->board, state->board_count);
  if (texture.valid) {
    if (texture.max_suit_count >= 4U) outs -= 10;
    if (texture.max_cards_in_five_rank_window >= 4U) outs -= 10;
  }

  if (outs < 0) outs = 0;
  if (outs > 150) outs = 150; /* the cap at fifteen */
  return outs;
}

/*
 * Outs needed to call, in tenths, at the price being asked. The anchors are
 * the three sizes worth naming: half the pot is 25 percent and needs six on
 * the flop, a pot-sized bet is 33 percent and needs nine, twice the pot is 40
 * percent and needs eleven. Twice those on the turn, where only one card is
 * coming. Everything between is interpolated, and past two pot the last slope
 * continues rather than flattening.
 */
static int required_outs_x10(int price_percent, bool flop) {
  static const int price[3] = {25, 33, 40};
  static const int on_flop[3] = {60, 90, 110};
  static const int on_turn[3] = {120, 160, 190};
  const int* needed = flop ? on_flop : on_turn;

  if (price_percent <= price[0]) {
    if (price_percent <= 0) return 0;
    return (needed[0] * price_percent) / price[0];
  }
  for (int index = 1; index < 3; index++) {
    if (price_percent <= price[index]) {
      const int span = price[index] - price[index - 1];
      return needed[index - 1] +
             ((needed[index] - needed[index - 1]) *
              (price_percent - price[index - 1])) /
                 span;
    }
  }
  return needed[2] + ((needed[2] - needed[1]) * (price_percent - price[2])) /
                         (price[2] - price[1]);
}

bool felt_draw_is_priced(const FeltGameState* state,
                         const FeltHandValue* value,
                         const FeltDraws* draws) {
  if (state == NULL || state->to_call <= 0 ||
      state->street == FELT_STREET_RIVER) {
    return false;
  }
  const int outs = felt_live_outs_x10(state, value, draws);
  if (outs <= 0) return false;
  return outs >= required_outs_x10(felt_call_price_percent(state),
                                   state->street == FELT_STREET_FLOP);
}

int felt_bluff_catch_frequency(const FeltGameState* state,
                               double delta,
                               const FeltRangeRead* read) {
  if (state == NULL || read == NULL || !read->valid || state->to_call <= 0) {
    return 0;
  }
  const bool raised = facing_raise(state);
  const int shift = raised ? RAISE_SHIFT : 0;
  /* Out of position the boundary of the calling range used to sit five
   * points higher, because a marginal call out of position realises less of
   * its equity. On the river there is nothing left to realise -- the hand is
   * decided by this call -- so both seats use the same boundary there. */
  const bool discount_position = state->street != FELT_STREET_RIVER;
  const int thin_floor =
      raised ? RAISE_THIN_CATCH + shift
             : (state->position == FELT_POSITION_BIG_BLIND &&
                        discount_position
                    ? RAISE_THIN_CATCH
                    : INITIAL_BET_THIN_CATCH);
  const int fraction = bet_fraction_percent(state);
  /* Minimum defence frequency for the actual wager is the centre of three
   * equally wide strength buckets. The strongest third calls at 1.5x MDF,
   * the middle at MDF, and the weakest at 0.5x MDF. If candidate hands were
   * spread evenly through the delta band, the uncapped multipliers would
   * average MDF. Unequal populations, the 100% cap, and read adjustments mean
   * this per-hand heuristic does not guarantee aggregate MDF defence.
   *
   * Using relative frequencies also removes the cliff at the old automatic
   * call boundary. A hand just below DELTA_CALL is now in the strongest
   * bucket instead of sharing one flat frequency with every hand down to
   * delta -10. */
  const int mdf = fraction >= 0 ? 10000 / (100 + fraction) : 0;
  int frequency;
  const double call_ceiling = (double)(DELTA_CALL + shift);
  if (delta < (double)thin_floor || delta >= call_ceiling) {
    return 0;
  }
  const double bucket_width = (call_ceiling - (double)thin_floor) / 3.0;
  if (delta >= call_ceiling - bucket_width) {
    frequency = (3 * mdf + 1) / 2; /* 1.5x, rounded to nearest */
  } else if (delta >= call_ceiling - 2.0 * bucket_width) {
    frequency = mdf;
  } else {
    frequency = (mdf + 1) / 2; /* 0.5x, rounded to nearest */
  }

  /* Being raised is a reason to fold, but not as much of one when the bet
   * they raised was already large: a raise of a big bet is a narrower action
   * than a raise of a small one, and we are getting a better price on it. */
  if (raised && !we_bet_large(state)) frequency -= 10;
  /* The table is neutral at P=R=50, where the estimate is 20%. Move its
   * frequency one point for every point the exact estimate differs. */
  frequency += (read->bluff_rate_basis_points - 2000) / 100;
  if (frequency < 0) frequency = 0;
  if (frequency > 100) frequency = 100;
  return frequency;
}

bool felt_forced_cheap_call(const FeltGameState* state,
                            const FeltHandValue* value) {
  if (state == NULL || value == NULL || !value->valid || state->to_call <= 0) {
    return false;
  }
  const FeltChips before =
      state->pot - state->to_call - state->my_street_contribution;
  if (before <= 0) return false;
  if (100 * state->to_call < 10 * before) return true;
  return 100 * state->to_call < 20 * before &&
         (state->street == FELT_STREET_RIVER || state->opp_stack == 0) &&
         value->player_made_pair_or_better;
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
  if (felt_forced_cheap_call(state, value)) return true;

  const bool raised = facing_raise(state);
  const double delta = felt_range_delta(value, read);
  if (delta >= (double)(DELTA_CALL + (raised ? RAISE_SHIFT : 0))) {
    return true;
  }
  if (felt_draw_is_priced(state, value, draws)) {
    return true;
  }
  const int frequency = felt_bluff_catch_frequency(state, delta, read);
  if ((int)((state->decision_random >> 8U) % UINT64_C(100)) < frequency) {
    return true;
  }
  return false;
}

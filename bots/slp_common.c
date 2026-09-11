#include "slp_common.h"

#include "felt/bot_kit.h"

#include <stdbool.h>

static FeltAction aggressive_action(const FeltGameState* state) {
  if (state->to_call > 0) {
    return felt_raise_to_multiple(state, 3U);
  }
  return felt_raise_to_pot_fraction(state, 0.75);
}

static bool is_pair_like_showdown(const FeltGameState* state,
                                  const FeltMadeHand* made,
                                  const FeltBoardTexture* texture) {
  if (!made->valid) {
    return false;
  }
  /* A hand the board made for both of us is showdown value and nothing more:
   * betting it charges the opponent nothing, because the opponent has it. And
   * only when a card of ours plays -- trips on the board with an ace is a
   * hand to check down, trips on the board with a seven is a fold. */
  if (made->category >= FELT_MADE_TRIPS &&
      !felt_hand_is_own(state->hole, state->board, state->board_count,
                        made, texture)) {
    return felt_kicker_plays(state->hole, state->board, state->board_count,
                             made, texture);
  }
  if (made->category == FELT_MADE_TWO_PAIR) {
    if (made->two_pair_kind == FELT_TWO_PAIR_BOARD_ONLY) {
      return felt_kicker_plays(state->hole, state->board, state->board_count,
                               made, texture);
    }
    return made->two_pair_kind == FELT_TWO_PAIR_UNDER ||
           made->two_pair_kind == FELT_TWO_PAIR_MIDDLE;
  }
  /* FELT_PAIR_NONE here means the pair is the board's and our two cards are
   * the kicker. That is a weak holding, not air: it beats a bluff at
   * showdown, and it has nothing to bet. It used to miss this test and fall
   * through to the air branch, where it was bet as a bluff 44% of the time
   * and folded to every bet -- one postflop hand in six.
   *
   * Whether it is a showdown hand at all is the kicker's question, though,
   * because a pair the board holds both of is the same hand for both players.
   * That is the rule trips-on-board has always followed, one category down
   * and far more common -- and without it queen-jack on 2c 5s 5h 4h Ks called
   * three barrels for 65 bb with a queen that never entered the hand. */
  if (made->category == FELT_MADE_ONE_PAIR &&
      made->pair_relation == FELT_PAIR_NONE) {
    return felt_kicker_plays(state->hole, state->board, state->board_count,
                             made, texture) &&
           felt_kicker_leads(state->hole, state->board, state->board_count);
  }
  return made->category == FELT_MADE_ONE_PAIR &&
         made->pair_relation != FELT_PAIR_TOP &&
         made->pair_relation != FELT_PAIR_OVERPAIR;
}

static bool is_slp_value_hand(const FeltGameState* state,
                              const FeltMadeHand* made,
                              const FeltBoardTexture* texture) {
  if (!made->valid) {
    return false;
  }
  if (made->category >= FELT_MADE_TRIPS) {
    /* Trips the board holds all three of, a flush or straight lying on the
     * board, a full house the board makes by itself: the category is high and
     * the hand is not ours. Those take the showdown line instead. */
    return felt_hand_is_own(state->hole, state->board,
                            state->board_count, made, texture);
  }
  if (made->category == FELT_MADE_TWO_PAIR) {
    return made->two_pair_kind == FELT_TWO_PAIR_OVER ||
           made->two_pair_kind == FELT_TWO_PAIR_BOTH_HOLE_CARDS;
  }
  return made->category == FELT_MADE_ONE_PAIR &&
         (made->pair_relation == FELT_PAIR_TOP ||
          made->pair_relation == FELT_PAIR_OVERPAIR);
}

/* Street-local test for facing a raise rather than an opening bet: chips
 * already committed on this street plus more still owed means the opponent
 * raised us. No history is read, so the policy stays street-local. */
static bool facing_raise(const FeltGameState* state) {
  return state->my_street_contribution > 0 && state->to_call > 0;
}

static bool is_overpair_or_better(const FeltGameState* state,
                                  const FeltMadeHand* made,
                                  const FeltBoardTexture* texture) {
  if (!made->valid) {
    return false;
  }
  if (made->category >= FELT_MADE_TRIPS) {
    return felt_hand_is_own(state->hole, state->board,
                            state->board_count, made, texture);
  }
  if (made->category == FELT_MADE_TWO_PAIR) {
    return made->two_pair_kind == FELT_TWO_PAIR_OVER ||
           made->two_pair_kind == FELT_TWO_PAIR_BOTH_HOLE_CARDS;
  }
  return made->category == FELT_MADE_ONE_PAIR &&
         made->pair_relation == FELT_PAIR_OVERPAIR;
}

static FeltAction slp_decide(const FeltGameState* state, SlpProfile profile) {
  if (state == NULL) {
    return felt_check_or_fold(state);
  }
  if (state->street == FELT_STREET_PREFLOP) {
    /* A heads-up minimum open lays the big blind 3-to-1. Defend every hand at
     * that price so a bot cannot profit by opening any two cards for 2 bb. */
    const FeltChips big_blind = felt_big_blind(state);
    if (profile == SLP_BALANCE && state->position == FELT_POSITION_BIG_BLIND &&
        state->to_call > 0 && big_blind > 0 &&
        state->opp_street_contribution == 2 * big_blind &&
        felt_preflop_raise_count(state) == 1U) {
      return felt_call_or_check(state);
    }
    return felt_preflop_baseline_action(state);
  }

  const FeltMadeHand made =
      felt_made_hand(state->hole, state->board, state->board_count);
  const FeltDraws draws =
      felt_draws(state->hole, state->board, state->board_count);
  const FeltBoardTexture texture =
      felt_board_texture(state->board, state->board_count);
  if (!made.valid || !draws.valid) {
    return felt_check_or_fold(state);
  }

  if (profile == SLP_EXPLOIT_FOLD && state->to_call > 0) {
    return is_overpair_or_better(state, &made, &texture) ? aggressive_action(state)
                                        : felt_check_or_fold(state);
  }
  /* Balance keeps the two genuinely strong two-pair bands out of its raising
   * range: facing a bet they call and never raise, the same restraint it puts
   * on a single pair. With nobody betting they take the ordinary value line
   * instead. Checking them every time was value the bot never collected --
   * every other slp profile bets these -- and the old carve-out only ever had
   * a reason for the raise. Under and middle two pair still continue through
   * the smaller-pair path, so they call an opening bet but fold to a raise. */
  if (profile == SLP_BALANCE && state->to_call > 0 &&
      made.category == FELT_MADE_TWO_PAIR &&
      (made.two_pair_kind == FELT_TWO_PAIR_OVER ||
       made.two_pair_kind == FELT_TWO_PAIR_BOTH_HOLE_CARDS)) {
    return felt_call_or_check(state);
  }
  if (is_slp_value_hand(state, &made, &texture)) {
    /* Balance never reraises a single pair. This is intentionally independent
     * of whether the aggression is an opening bet or a raise of our own bet. */
    if (profile == SLP_BALANCE && state->to_call > 0 &&
        made.category == FELT_MADE_ONE_PAIR) {
      return felt_call_or_check(state);
    }
    /* Trips or better uses a per-decision 33% trap / 67% aggressive split.
     *
     * Never on the river facing a bet. A trap is a call now in exchange for a
     * bigger pot later, and on the last street there is no later: calling the
     * nuts is simply the raise not made. It cost the whole remaining stack on
     * a board of Qd Ks 5h 2d 8d, where the nut flush faced a re-raise, rolled
     * a trap, and called off with a jack-high flush already committed. */
    const bool nothing_left_to_induce =
        state->street == FELT_STREET_RIVER && state->to_call > 0;
    if (profile == SLP_BALANCE && !nothing_left_to_induce &&
        state->decision_random % UINT64_C(3) == 0U) {
      return felt_call_or_check(state);
    }
    return aggressive_action(state);
  }
  if (is_pair_like_showdown(state, &made, &texture) ||
      draws.flags != FELT_DRAW_NONE) {
    /* Against an opening bet these always continue: folding them to a bet
     * larger than the prior pot used to cost roughly 8 bb/hand against a
     * bluff-heavy opponent. Against a raise, a third of the draws carry on --
     * enough to stay unpredictable without paying off a nutted range -- and
     * small pairs give up. */
    if (profile == SLP_BALANCE && facing_raise(state)) {
      const bool has_draw = draws.flags != FELT_DRAW_NONE;
      if (has_draw && (state->decision_random >> 16U) % UINT64_C(3) == 0U) {
        return felt_call_or_check(state);
      }
      return felt_check_or_fold(state);
    }
    return felt_call_or_check(state);
  }

  if (profile == SLP_BALANCE && state->to_call > 0) {
    return felt_check_or_fold(state);
  }
  if (profile == SLP_BLUFF ||
      profile == SLP_EXPLOIT_FOLD ||
      (profile == SLP_BALANCE &&
       (state->decision_random & UINT64_C(1)) != 0U)) {
    return felt_bluff_action(state, aggressive_action(state));
  }
  return felt_check_or_fold(state);
}

/*
 * A river bet this small is a price, not a bet. Folding a made hand for a
 * seventh of the pot has to be right more than six times in seven, and no
 * street-local read is that good -- slp-odds folded pocket jacks to 2950 into
 * 20949 rather than call and be shown a bluff. So any pair or better calls,
 * whatever the policy above decided.
 *
 * Only a fold is overridden. A hand that wanted to raise still raises, and a
 * character whose whole point is folding to size is unaffected, because at a
 * seventh of the pot no size rule has fired.
 */
static bool river_price_is_token(const FeltGameState* state) {
  return state != NULL && state->street == FELT_STREET_RIVER &&
         state->to_call > 0 && state->pot > 0 &&
         (long long)state->to_call * 100LL <= (long long)state->pot * 15LL;
}

FeltAction slp_act(const FeltGameState* state, SlpProfile profile) {
  const FeltAction action = slp_decide(state, profile);
  if (action.type != FELT_ACTION_FOLD || !river_price_is_token(state)) {
    return action;
  }
  const FeltMadeHand made =
      felt_made_hand(state->hole, state->board, state->board_count);
  if (!made.valid || made.category < FELT_MADE_ONE_PAIR) {
    return action;
  }
  return felt_call_or_check(state);
}

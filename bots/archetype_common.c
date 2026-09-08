#include "archetype_common.h"

#include "slp_common.h"

#include "felt/bot_kit.h"

#include <stdbool.h>
#include <stddef.h>

/* ---------------------------------------------------------------- */
/* History and sizing helpers                                        */
/* ---------------------------------------------------------------- */

static FeltChips big_blind_of(const FeltGameState* state) {
  for (uint32_t i = 0; i < state->history_count; i++) {
    if (state->history[i].type == FELT_EVENT_POST_BIG_BLIND &&
        state->history[i].amount_to > 0) {
      return state->history[i].amount_to;
    }
  }
  return 100;
}

static FeltChips bb_units(FeltChips chips, FeltChips big_blind) {
  return big_blind > 0 ? chips / big_blind : 0;
}

/* Voluntary preflop raises so far. 1 = opened, 2 = 3-bet, 3+ = 4-bet or more. */
static uint32_t preflop_raise_count(const FeltGameState* state) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < state->history_count; i++) {
    const FeltActionEvent* event = &state->history[i];
    if (event->street != FELT_STREET_PREFLOP) continue;
    if (event->type == FELT_EVENT_BET || event->type == FELT_EVENT_RAISE) {
      count++;
    }
  }
  return count;
}

/* True when this bot made the last voluntary preflop raise. */
static bool is_preflop_aggressor(const FeltGameState* state) {
  bool mine = false;
  bool seen = false;
  for (uint32_t i = 0; i < state->history_count; i++) {
    const FeltActionEvent* event = &state->history[i];
    if (event->street != FELT_STREET_PREFLOP) continue;
    if (event->type == FELT_EVENT_BET || event->type == FELT_EVENT_RAISE) {
      mine = event->position == state->position;
      seen = true;
    }
  }
  return seen && mine;
}

/* Voluntary bets or raises by anyone on the current street. */
static uint32_t street_raise_count(const FeltGameState* state) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < state->history_count; i++) {
    const FeltActionEvent* event = &state->history[i];
    if (event->street != state->street) continue;
    if (event->type == FELT_EVENT_BET || event->type == FELT_EVENT_RAISE) {
      count++;
    }
  }
  return count;
}

/* Heads-up: the button acts last on every street after the flop. */
static bool in_position(const FeltGameState* state) {
  return state->position == FELT_POSITION_BUTTON;
}

static bool can_raise(const FeltGameState* state) {
  return (state->legal_actions & FELT_LEGAL_RAISE_TO) != 0U;
}

static FeltAction min_raise(const FeltGameState* state) {
  FeltAction action = {0};
  if (!can_raise(state)) {
    return felt_call_or_check(state);
  }
  action.type = FELT_ACTION_RAISE_TO;
  action.amount_to = state->max_raise_to < state->min_raise_to
                         ? state->max_raise_to
                         : state->min_raise_to;
  return action;
}

/* ---------------------------------------------------------------- */
/* Hand-strength predicates                                          */
/* ---------------------------------------------------------------- */

static bool any_pair_or_better(const FeltMadeHand* made) {
  if (!made->valid) return false;
  if (made->category >= FELT_MADE_TRIPS) return true;
  if (made->category == FELT_MADE_TWO_PAIR) {
    return made->two_pair_kind != FELT_TWO_PAIR_NONE &&
           made->two_pair_kind != FELT_TWO_PAIR_BOARD_ONLY;
  }
  return made->category == FELT_MADE_ONE_PAIR &&
         made->pair_relation != FELT_PAIR_NONE;
}

static bool straight_or_better(const FeltMadeHand* made) {
  return made->valid && made->category >= FELT_MADE_STRAIGHT;
}

static bool strong_two_pair_or_better(const FeltMadeHand* made) {
  if (!made->valid) return false;
  if (made->category >= FELT_MADE_TRIPS) return true;
  return made->category == FELT_MADE_TWO_PAIR &&
         (made->two_pair_kind == FELT_TWO_PAIR_OVER ||
          made->two_pair_kind == FELT_TWO_PAIR_BOTH_HOLE_CARDS);
}

static bool quads_or_better(const FeltMadeHand* made) {
  return made->valid && made->category >= FELT_MADE_QUADS;
}

static bool has_draw(const FeltDraws* draws) {
  /* Bare overcards are not a draw for these archetypes. */
  return draws->valid &&
         (draws->flags & (FELT_DRAW_GUTSHOT | FELT_DRAW_OPEN_ENDED |
                          FELT_DRAW_DOUBLE_GUTSHOT | FELT_DRAW_FLUSH)) != 0U;
}

static uint32_t rank_of(FeltCard card) { return (uint32_t)(card >> 2U); }
static uint32_t suit_of(FeltCard card) { return (uint32_t)(card & 3U); }

static bool suited(const FeltGameState* state) {
  return suit_of(state->hole[0]) == suit_of(state->hole[1]);
}

/* TT+, ATo/ATs+, KQ -- Nancy's opening requirement. */
static bool nancy_playable(const FeltGameState* state) {
  const uint32_t a = rank_of(state->hole[0]);
  const uint32_t b = rank_of(state->hole[1]);
  const uint32_t high = a > b ? a : b;
  const uint32_t low = a > b ? b : a;
  if (a == b) return a >= 8U;                 /* TT+ */
  if (high == 12U) return low >= 8U;          /* AT+ */
  if (high == 11U) return low >= 10U;         /* KQ  */
  return false;
}

/* AA, KK, QQ, AK -- the only hands Nancy 3-bets or continues against one. */
static bool nancy_premium(const FeltGameState* state) {
  const uint32_t a = rank_of(state->hole[0]);
  const uint32_t b = rank_of(state->hole[1]);
  const uint32_t high = a > b ? a : b;
  const uint32_t low = a > b ? b : a;
  if (a == b) return a >= 10U;                /* QQ+ */
  return high == 12U && low == 11U;           /* AK  */
}

/* ---------------------------------------------------------------- */
/* Profiles                                                          */
/* ---------------------------------------------------------------- */

static FeltAction default_action(const FeltGameState* state) {
  return slp_act(state, SLP_BALANCE);
}

static FeltAction preflop_default(const FeltGameState* state) {
  return felt_preflop_baseline_action(state);
}

/* Downgrade a raise to a call, keeping everything else. */
static FeltAction without_raising(const FeltGameState* state, FeltAction action) {
  if (action.type == FELT_ACTION_RAISE_TO) {
    return felt_call_or_check(state);
  }
  return action;
}

FeltAction archetype_act(const FeltGameState* state, ArchetypeProfile profile) {
  if (state == NULL) {
    return felt_check_or_fold(state);
  }

  const FeltChips bb = big_blind_of(state);
  const bool preflop = state->street == FELT_STREET_PREFLOP;

  FeltMadeHand made = {0};
  FeltDraws draws = {0};
  if (!preflop) {
    made = felt_made_hand(state->hole, state->board, state->board_count);
    draws = felt_draws(state->hole, state->board, state->board_count);
    if (!made.valid || !draws.valid) {
      return felt_check_or_fold(state);
    }
  }

  FeltAction action;

  switch (profile) {
    /* ---------------------------------------------------------- */
    case ARCHETYPE_NITTY_NANCY:
      if (preflop) {
        const uint32_t raises = preflop_raise_count(state);
        if (raises >= 2U) {
          /* Facing a 3-bet or worse: only the premiums continue, and they
           * re-raise rather than call. */
          return nancy_premium(state) ? felt_raise_to_multiple(state, 3U)
                                      : felt_check_or_fold(state);
        }
        if (!nancy_playable(state)) return felt_check_or_fold(state);
        if (raises == 1U) {
          /* Facing an open: 3-bet the premiums, call the rest of the range. */
          return nancy_premium(state) ? felt_raise_to_multiple(state, 3U)
                                      : felt_call_or_check(state);
        }
        return nancy_premium(state) ? felt_raise_to_multiple(state, 3U)
                                    : felt_call_or_check(state);
      }
      if (state->to_call > 0) {
        if (straight_or_better(&made)) return felt_raise_to_pot_fraction(state, 0.75);
        return felt_is_top_pair_or_better(&made) ? felt_call_or_check(state)
                                                 : felt_check_or_fold(state);
      }
      /* Never bluffs: value bets top pair or better, otherwise checks. */
      return felt_is_top_pair_or_better(&made) ? felt_raise_to_pot_fraction(state, 0.75)
                                               : felt_check_or_fold(state);

    /* ---------------------------------------------------------- */
    case ARCHETYPE_CALLING_STATION:
      if (preflop) {
        /* Any hand the SB opening chart does not fold, called at any price. */
        const FeltPreflopChartAction chart = felt_preflop_baseline_lookup(
            FELT_PREFLOP_SB_FIRST_IN, state->hole[0], state->hole[1]);
        return chart == FELT_PREFLOP_CHART_FOLD ? felt_check_or_fold(state)
                                                : felt_call_or_check(state);
      }
      /* Any pair and any draw call, at any price. Draws are spelled out
       * because the shared policy folds most of them to a raise. */
      if (state->to_call > 0 && (any_pair_or_better(&made) || has_draw(&draws))) {
        return felt_call_or_check(state);
      }
      return default_action(state);

    /* ---------------------------------------------------------- */
    case ARCHETYPE_PASSIVE_PATTY:
      if (preflop) {
        return without_raising(state, preflop_default(state));
      }
      if (quads_or_better(&made)) {
        return felt_raise_to_pot_fraction(state, 0.75);
      }
      if (felt_is_top_pair_or_better(&made)) {
        return felt_call_or_check(state);
      }
      return felt_check_or_fold(state);

    /* ---------------------------------------------------------- */
    case ARCHETYPE_CHASING_CHARLIE:
      if (preflop) {
        /* Any suited hand calls, but only up to a medium raise. The bot kit's
         * medium bucket ends at 22 bb to call, so that is the number here. */
        if (suited(state) && bb_units(state->to_call, bb) < 22) {
          return felt_call_or_check(state);
        }
        return preflop_default(state);
      }
      if (has_draw(&draws) && state->to_call > 0) {
        return felt_call_or_check(state);
      }
      return default_action(state);

    /* ---------------------------------------------------------- */
    case ARCHETYPE_SEMI_BLUFF_SARAH:
      if (preflop) return preflop_default(state);
      if (has_draw(&draws)) {
        return state->to_call > 0 ? felt_raise_to_multiple(state, 3U)
                                  : felt_raise_to_pot_fraction(state, 0.75);
      }
      if (felt_is_top_pair_or_better(&made) || any_pair_or_better(&made)) {
        return default_action(state);
      }
      return felt_check_or_fold(state); /* folds all other air */

    /* ---------------------------------------------------------- */
    case ARCHETYPE_TRAPPING_THOMAS:
      if (preflop) {
        /* Trap the premium range instead of raising it. All other hands stay
         * on the shared chart. */
        if (nancy_premium(state)) return felt_call_or_check(state);
        return preflop_default(state);
      }
      if (felt_is_top_pair_or_better(&made)) {
        /* A third of the time the turn trap springs early. Only after a bet,
         * so it is always a check-raise and never an opening bet, and never
         * preflop -- there is nothing to check there. */
        if (state->street == FELT_STREET_TURN && state->to_call > 0 &&
            (state->decision_random >> 24U) % UINT64_C(3) == 0U) {
          return felt_raise_to_multiple(state, 3U);
        }
        if (state->street != FELT_STREET_RIVER) {
          /* Flop and turn: check back in position and check-call out of
           * position. If the opponent leads into position, calling preserves
           * the same slow-play line. */
          return felt_call_or_check(state);
        }
        if (in_position(state)) {
          /* On the river, take normal value action when last to act. */
          return state->to_call > 0 ? felt_raise_to_multiple(state, 3U)
                                    : felt_raise_to_pot_fraction(state, 0.75);
        }
        /* Out of position, spring the trap only after a river bet. */
        if (state->to_call > 0) return felt_raise_to_multiple(state, 4U);
        return felt_call_or_check(state);
      }
      /* Never bluffs. */
      return state->to_call > 0 ? felt_check_or_fold(state)
                                : felt_call_or_check(state);

    /* ---------------------------------------------------------- */
    case ARCHETYPE_TRIPLE_BARREL_TRAVIS:
      if (preflop) return preflop_default(state);
      if (is_preflop_aggressor(state) && street_raise_count(state) == 0U) {
        return felt_raise_to_pot_fraction(state, 0.75);
      }
      return default_action(state);

    /* ---------------------------------------------------------- */
    case ARCHETYPE_ONE_AND_DONE:
      if (preflop) return preflop_default(state);
      if (is_preflop_aggressor(state) && state->street == FELT_STREET_FLOP &&
          street_raise_count(state) == 0U) {
        return felt_raise_to_pot_fraction(state, 0.75);
      }
      if (is_preflop_aggressor(state) && state->street > FELT_STREET_FLOP &&
          !any_pair_or_better(&made) && !has_draw(&draws)) {
        return felt_check_or_fold(state); /* gives up after the one barrel */
      }
      return default_action(state);

    /* ---------------------------------------------------------- */
    case ARCHETYPE_SCARED_SAM:
      if (preflop) {
        /* Above 25 bb to call, the hands he would raise only call. Keyed off
         * the raise in front of him, the same scale his postflop rules use. */
        if (bb_units(state->to_call, bb) >= 25) {
          return without_raising(state, preflop_default(state));
        }
        return preflop_default(state);
      }
      if (bb_units(state->to_call, bb) >= 50) {
        return straight_or_better(&made) ? felt_call_or_check(state)
                                         : felt_check_or_fold(state);
      }
      if (bb_units(state->to_call, bb) >= 25) {
        return strong_two_pair_or_better(&made) ? felt_call_or_check(state)
                                                : felt_check_or_fold(state);
      }
      if (bb_units(state->pot, bb) >= 50) {
        return without_raising(state, default_action(state));
      }
      return default_action(state);

    /* ---------------------------------------------------------- */
    case ARCHETYPE_TILTED_TERRY:
      if (preflop) {
        /* Calls any open with anything, but only up to 45 bb. Past that the
         * raise is all-in sized and he is back on the chart, so he no longer
         * stacks off with seven-deuce. */
        if (preflop_raise_count(state) <= 1U && state->to_call > 0 &&
            bb_units(state->to_call, bb) < 45) {
          return felt_call_or_check(state);
        }
        return preflop_default(state);
      }
      if (any_pair_or_better(&made)) {
        return felt_call_or_check(state);
      }
      /* Bluffs all air, but folds it once re-raised. */
      if (street_raise_count(state) >= 2U) {
        return felt_check_or_fold(state);
      }
      return state->to_call > 0 ? felt_raise_to_multiple(state, 3U)
                                : felt_raise_to_pot_fraction(state, 0.75);

    /* ---------------------------------------------------------- */
    case ARCHETYPE_MIN_RAISE_MIRANDA:
      action = preflop ? preflop_default(state) : default_action(state);
      return action.type == FELT_ACTION_RAISE_TO ? min_raise(state) : action;

    /* ---------------------------------------------------------- */
    case ARCHETYPE_OVERBET_OLIVER:
      action = preflop ? preflop_default(state) : default_action(state);
      return action.type == FELT_ACTION_RAISE_TO
                 ? felt_raise_to_pot_fraction(state, 2.0)
                 : action;

    /* ---------------------------------------------------------- */
    case ARCHETYPE_CHECK_RAISE_CHALAMET:
      if (preflop) {
        /* Limps every hand he plays rather than opening; in the big blind,
         * where there is nothing to limp into, he is on the shared chart. */
        if (preflop_raise_count(state) == 0U &&
            state->position == FELT_POSITION_BUTTON) {
          return felt_call_or_check(state);
        }
        return preflop_default(state);
      }
      /* He never opens the betting on any street. Everything he would have
       * bet -- top pair or better for value, any draw as the bluff -- waits
       * for the opponent to bet and comes back as a raise to three times. */
      if (state->to_call == 0) {
        return felt_call_or_check(state);
      }
      if (felt_is_top_pair_or_better(&made) || has_draw(&draws)) {
        return felt_raise_to_multiple(state, 3U);
      }
      return without_raising(state, default_action(state));

    /* ---------------------------------------------------------- */
    case ARCHETYPE_AGGRESSIVE_ANDY:
      if (preflop) {
        /* Opens or isolates a limp with any two cards, but only for a small
         * raise. An existing raise puts him on the chart instead of making
         * him automatically 3-bet. */
        if (preflop_raise_count(state) == 0U && can_raise(state)) {
          return felt_raise_to_multiple(state, 2U);
        }
        return preflop_default(state);
      }
      /* Aggression is the one thing that slows him down. */
      if (state->to_call > 0) {
        return default_action(state);
      }
      /* Unbet pot: any pair is a bet, and so is air whenever he is the one
       * telling the story -- in position against a check, or out of position
       * as the preflop raiser. */
      if (any_pair_or_better(&made) || in_position(state) ||
          is_preflop_aggressor(state)) {
        return felt_raise_to_pot_fraction(state, 0.75);
      }
      return default_action(state);
  }

  return default_action(state);
}

/*
 * solved-all-in: the best a pure shove-or-fold strategy can do at 200 bb.
 *
 * Ranges come from solvers/push_fold, which finds an approximate equilibrium of
 * the restricted game where both players may only shove or fold preflop. At
 * 200 bb that game is barely worth playing -- risking 200 bb to win 1.5 bb needs
 * an enormous edge -- so the solved ranges are extremely tight:
 *
 *   SB open shove            AA KK QQ AKs AKo AQs AJs ATs A5s A4s (4.1%)
 *   BB vs limp/small raise   AA KK                                (0.9%)
 *   BB vs large raise/all-in AA KK QQ AKs                         (1.7%)
 *
 * A4s and A5s look out of place beside ATs but are genuine: wheel aces pick up
 * straight equity and block the ace-heavy calling range.
 *
 * A partial raise below 75 bb shares the limp/small-raise response. A raise to
 * 75 bb or more shares the large-raise/all-in response. The 75 bb boundary is a
 * transparent heuristic: at the default 200 bb depth it commits 37.5% of the
 * stack, while keeping ordinary opens out of the all-in bucket.
 *
 * The small-raise bucket is TIGHTER than the large/all-in bucket, which looks
 * wrong and is not. Facing an all-in you are up against the wide 4.1% shoving
 * range, so QQ and AKs are fine. Re-shoving 200 bb over a 2 bb raise wins 2 bb
 * when it works and runs into a premium when it does not, so it needs a premium
 * of its own.
 * The deeper cause is that this bot cannot call: folding QQ to a min-raise is
 * absurd in real poker, but its only alternative is a 200 bb shove.
 *
 * This bot never plays a flop by choice. If it checks the big blind back it
 * gives up postflop, which is the honest cost of being preflop-only.
 */

#include "felt/bot_api.h"

#include "push_fold_table.h"

static uint32_t rank_of(FeltCard card) { return (uint32_t)(card >> 2U); }
static uint32_t suit_of(FeltCard card) { return (uint32_t)(card & 3U); }

/* Must match solvers/push_fold: pairs on the diagonal, suited at low*13+high,
 * offsuit at high*13+low. */
static uint32_t class_index(FeltCard a, FeltCard b) {
  const uint32_t ra = rank_of(a);
  const uint32_t rb = rank_of(b);
  const uint32_t high = ra > rb ? ra : rb;
  const uint32_t low = ra > rb ? rb : ra;
  if (ra == rb) {
    return high * 13U + low;
  }
  return (suit_of(a) == suit_of(b)) ? low * 13U + high : high * 13U + low;
}

/* The big blind is not a state field, so read it back from the forced post. */
static FeltChips big_blind_of(const FeltGameState* state) {
  uint32_t i;
  for (i = 0; i < state->history_count; i++) {
    if (state->history[i].type == FELT_EVENT_POST_BIG_BLIND) {
      return state->history[i].amount_to;
    }
  }
  return 100; /* only reachable if the history is unavailable */
}

static FeltAction shove(const FeltGameState* state) {
  FeltAction action = {0};
  if ((state->legal_actions & FELT_LEGAL_RAISE_TO) != 0U) {
    action.type = FELT_ACTION_RAISE_TO;
    action.amount_to = state->max_raise_to;
    return action;
  }
  /* Cannot raise: the opponent is already all-in, so calling is the shove. */
  if ((state->legal_actions & FELT_LEGAL_CALL) != 0U) {
    action.type = FELT_ACTION_CALL;
    return action;
  }
  action.type = (state->legal_actions & FELT_LEGAL_CHECK) != 0U
                    ? FELT_ACTION_CHECK
                    : FELT_ACTION_FOLD;
  return action;
}

static FeltAction give_up(const FeltGameState* state) {
  FeltAction action = {0};
  action.type = (state->legal_actions & FELT_LEGAL_FOLD) != 0U
                    ? FELT_ACTION_FOLD
                    : FELT_ACTION_CHECK;
  return action;
}

uint32_t felt_bot_abi_version(void) { return FELT_BOT_ABI_VERSION; }

const char* felt_bot_name(void) { return "solved-all-in"; }

FeltAction felt_bot_act(const FeltGameState* state) {
  unsigned flags;

  /* Reached a flop only by checking the big blind back; give up from here. */
  if (state->street != FELT_STREET_PREFLOP) {
    return give_up(state);
  }

  flags = felt_push_fold[class_index(state->hole[0], state->hole[1])];

  if (state->position == FELT_POSITION_BUTTON) {
    return (flags & FELT_PF_SB_OPEN) != 0U ? shove(state) : give_up(state);
  }

  {
    const FeltChips big_blind = big_blind_of(state);
    const int large_raise =
        state->opp_stack == 0 ||
        state->opp_street_contribution / big_blind >= 75;
    const unsigned bucket = large_raise ? FELT_PF_BB_VS_LARGE
                                        : FELT_PF_BB_VS_LIMP_OR_SMALL;
    return (flags & bucket) != 0U ? shove(state) : give_up(state);
  }
}

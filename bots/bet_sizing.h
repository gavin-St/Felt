#ifndef FELT_BET_SIZING_H
#define FELT_BET_SIZING_H

#include "felt/bot_api.h"
#include "felt/bot_kit.h"

#include "range_read.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sizing, kept entirely separate from the decision to put chips in. By the
 * time anything here runs, the bot has already decided to bet or raise and
 * why; the only question left is how much.
 *
 * Every intent has two candidate sizes rather than one, and which of the two
 * gets used is a weighted draw. One size is readable; two are not, and the
 * weight is where everything else gets to have an opinion -- the board, our
 * own outs, how weak their range is, and how close each candidate lands to
 * the size that would get the stacks in.
 *
 * The pair itself is chosen by two things: whether we are raising a raise,
 * and whether their range is polarised. A polarised range has nothing in the
 * middle to punish, so bluffing it big is burning money and value betting it
 * big is fine; a merged range is full of medium hands, so the sizes invert.
 */

typedef enum FeltSizingIntent {
  FELT_SIZING_VALUE = 0,
  FELT_SIZING_THIN_VALUE = 1,
  FELT_SIZING_BLUFF = 2
} FeltSizingIntent;

typedef struct FeltSizing {
  /* Fraction of the pot after calling; what felt_raise_to_pot_fraction wants. */
  double fraction;
  /* The two candidates and the odds the larger one was given, so a test or a
   * write-up can see the decision rather than just its outcome. */
  double small;
  double large;
  int weight_large;
  bool took_large;
} FeltSizing;

FeltSizing felt_choose_size(const FeltGameState* state,
                            const FeltRangeRead* read,
                            const FeltBoardTexture* texture,
                            const FeltDraws* draws,
                            FeltSizingIntent intent,
                            bool facing_raise,
                            int geometric_percent);

#ifdef __cplusplus
}
#endif

#endif

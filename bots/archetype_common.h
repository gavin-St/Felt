#ifndef FELT_ARCHETYPE_COMMON_H
#define FELT_ARCHETYPE_COMMON_H

#include "felt/bot_api.h"

/*
 * Tier 3 player archetypes. Each is the shared default -- the built-in preflop
 * chart plus the slp-balance postflop policy -- with one named deviation.
 * Unlike the slp bots these read state->history, so they are no longer
 * street-local.
 */
typedef enum ArchetypeProfile {
  ARCHETYPE_NITTY_NANCY = 0,
  ARCHETYPE_CALLING_STATION = 1,
  ARCHETYPE_PASSIVE_PATTY = 2,
  ARCHETYPE_CHASING_CHARLIE = 3,
  ARCHETYPE_SEMI_BLUFF_SARAH = 4,
  ARCHETYPE_TRAPPING_THOMAS = 5,
  ARCHETYPE_TRIPLE_BARREL_TRAVIS = 6,
  ARCHETYPE_ONE_AND_DONE = 7,
  ARCHETYPE_SCARED_SAM = 8,
  ARCHETYPE_TILTED_TERRY = 9,
  ARCHETYPE_MIN_RAISE_MIRANDA = 10,
  ARCHETYPE_OVERBET_OLIVER = 11,
  ARCHETYPE_AGGRESSIVE_ANDY = 12,
  ARCHETYPE_CHECK_RAISE_CHALAMET = 13
} ArchetypeProfile;

FeltAction archetype_act(const FeltGameState* state, ArchetypeProfile profile);

#endif

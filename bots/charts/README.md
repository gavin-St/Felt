# Built-in preflop baseline

`baseline_100bb_v1` is a deliberately simple heads-up small-blind-versus-big-
blind chart. It is based on the user-provided 100 bb reference charts and is
implemented in `harness/src/preflop_chart.cpp`.

It is a shared baseline for early postflop bots, not a solved or GTO strategy.
Felt currently starts matches at 200 bb, so deeper decisions are intentionally
conservative and should be replaced after the first postflop policies exist.

## Primary ranges

The chart uses whole 169-hand classes with no mixed frequencies. The first two
screenshots supply the two complete opening ranges:

| Spot | Value raise | Bluff raise | Passive | Fold |
|---|---:|---:|---:|---:|
| SB first in | 120 combos | 176 | 638 limps | 392 |
| BB versus a small raise | 124 combos | 188 | 726 calls | 288 |
| BB versus SB limp | 124 combos | 188 | 1,014 checks | 0 |

The supplied SB chart reports 118 value-raise, 172 bluff-raise, 644 limp, and
392 fold combinations. Converting its visible colors to pure 169-class cells
produces 120/176/638/392, a six-combination difference. We preserve the exact
fold region and accept the small approximation rather than add suit-specific or
mixed-frequency exceptions to a general baseline.

The BB-versus-small-raise transcription matches the supplied
124/188/726/288 counts exactly. BB-versus-limp is not in the supplied images;
it raises those same value and bluff classes, then checks everything else.

## Raise-size buckets

Response charts are selected by the opponent's total preflop contribution,
not by whether the action is conventionally called an open, 3-bet, 4-bet, or
5-bet:

| Amount facing us | Runtime chart |
|---|---|
| Less than 10 bb | Small raise; BB and SB use their position-specific source charts |
| 10 bb to less than 40 bb | Medium raise; based on the supplied SB-versus-3-bet chart |
| 40 bb to less than 75 bb | Large raise; conservative call/shove response |
| 75 bb or more | All-in-sized raise; tight call response |

An actual all-in is classified by its size too. This avoids treating a short
all-in like a 100 bb shove. For example, a first raise directly to 50 bb uses
the large-raise chart even though it is technically only an opening raise.
If the chart marks a hand as a bluff re-raise but the opponent is already
all-in, that hand folds; a value re-raise falls back to calling.

## Responses derived from the supplied charts

The supplied SB-open-versus-BB-3-bet range is the basis of the medium-raise
bucket:

- 4-bet value: `AKs AQs AJs AQo QQ JJ` (36 combos)
- 4-bet bluff: `J4s Q5o Q4o K3o K2o` (52)
- call: `ATs KQs KJs QJs KQo AJo KJo ATo TT 99 88 95s 85s 74s 43s`
  (98)
- fold everything else from the source node

The source chart omits `AA`, `KK`, and `AKo` because those hands limp in the
supplied first-in chart and therefore cannot reach its open/3-bet node. The
generalized medium bucket adds those hands to the value-raise range so an
unorthodox raise never makes the baseline fold a premium. Its complete counts
are 60 value-raise, 52 bluff-raise, 98 call, and 1,116 fold combinations.

The SB small-raise bucket starts with the supplied SB-limp-versus-BB-raise
range:

- limp/3-bet value: `AA AKo KK` (24 combos)
- limp/3-bet bluff: `Q7o K6o K5o A3o A2o` (60)
- limp/fold: `K4o Q6o J7o T7o 65o` (60)
- call every other hand in the SB limping range (494 in the pure-class
  approximation)

To make that chart safe outside its original limp-only path, hands from the SB
opening range are completed as follows: value opens remain value raises, bluff
opens become calls, and first-in folds remain folds. The complete counts are
144 value-raise, 60 bluff-raise, 670 passive, and 452 fold combinations.

## Deeper actions added for completeness

These were not fully specified by the images and are intentionally conservative:

- versus a 40-to-under-75 bb raise, shove `QQ+ AKs AKo`;
- call with `JJ TT AQs AJs KQs`;
- fold the remainder;
- versus a raise of at least 75 bb, call `QQ+ AKs AKo` and fold the
  remainder.

These are 100 bb assumptions. At Felt's current 200 bb default they are only a
temporary baseline, especially the shove range.

## Deliberately incorrect comparison chart

`action_count_v0` preserves the earlier action-count routing as an experimental
control. It ignores raise size and treats the first raise as small, the second
as medium, and every later raise as large. It never selects the all-in-sized
bucket.

Consequently, a first raise directly to 200 bb still receives the wide
BB-versus-small-raise range. If that range asks for either a value or bluff
re-raise when the opponent is already all-in, the old fallback turns it into a
call. This behavior is intentionally wrong and must not be used as the normal
baseline; it exists so a bot can measure the cost of ignoring bet size.

## Raise sizes

All sizes are total preflop contributions:

| Action | Size |
|---|---:|
| SB open | 2.5 bb |
| BB raise versus limp | 4 bb |
| BB re-raise versus a sub-10 bb raise | Greater of 10 bb or 4× the incoming size |
| SB re-raise versus a sub-10 bb raise | Greater of 12 bb or 3× the incoming size |
| Re-raise versus a 10-to-under-40 bb raise | Greater of 24 bb or 2.4× the incoming size |
| Raise versus 40 bb or more | All-in |

The direct action helper clamps the result into the harness's legal range and
handles short all-ins. Value and bluff labels currently use the same size; the
distinction is retained so later bots and reports can inspect the intended role.

## Public API

- `felt_preflop_class()` returns the canonical 169-class index.
- `felt_preflop_baseline_lookup()` exposes a pure hand-plus-spot lookup.
- `felt_preflop_baseline_decision()` uses history to recognize unopened and
  limped pots, then uses the amount facing the bot to select a raise bucket.
- `felt_preflop_baseline_action()` returns a legal `FeltAction` directly.
- `felt_preflop_action_count_v0_decision()` and
  `felt_preflop_action_count_v0_action()` expose the deliberately incorrect
  action-count variant.

All postflop bots should call the same direct helper preflop until this baseline
is intentionally versioned or replaced.

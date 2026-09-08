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

## Call-size buckets

Response charts are selected by the additional amount the bot must call,
measured in big blinds. They are not selected by the opponent's total preflop
contribution or by whether the action is conventionally called an open, 3-bet,
4-bet, or 5-bet:

| Additional amount to call | Runtime chart |
|---|---|
| Less than 6 bb | Small raise; BB and SB use their position-specific source charts |
| 6 bb to less than 22 bb | Medium raise; based on the supplied SB-versus-3-bet chart |
| 22 bb to less than 45 bb | Large raise; conservative call/shove response |
| 45 bb or more | All-in-sized raise; tight jam-or-fold response |

An actual all-in is classified by the remaining call size too. This avoids
treating a short all-in like a 100 bb shove. For example, a first raise to 40
bb against a posted 1 bb blind costs 39 bb to call and uses the large-raise
chart even though it is technically only an opening raise. Conversely, a late
raise to 76 bb after the bot has already contributed 48 bb costs only 28 bb to
call and uses the large chart.
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
unorthodox raise never makes the baseline fold a premium. What the shipped
chart actually plays, after that generalization, is:

- 4-bet value: `AA KK QQ JJ TT AKs AKo AQs AQo AJs` (66 combos)
- 4-bet bluff: `A5s A4s KTs K9s 87s` (20)
- call: `ATs A9s A8s A7s A6s A3s A2s KQs KJs QJs QTs Q9s JTs J9s T9s 98s
  76s 65s 54s KQo KJo QJo AJo ATo 99 88 77 66 55 44 33 22` (184)
- fold everything else (1,056)

The bucket starts at 6 bb to call rather than 10, so it now answers an
ordinary three-bet and not only a four-bet. The source range defended 15% of
all hands, which is right against a four-bet and much too tight against a
three-bet; the rest of the pairs, the suited aces, the one-gap suited
broadways and queen-jack offsuit were added as calls, taking the bucket to
20.4%.

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
160 value-raise, 60 bluff-raise, 510 passive, and 596 fold combinations.

## Deeper actions added for completeness

These were not fully specified by the images and are intentionally conservative:

- with 22 to under 45 bb left to call, shove `QQ+ AKs AKo` (34 combos);
- call with `JJ TT AQs AQo AJs AJo KQs T9s 87s 76s 65s` (64);
- fold the remainder (1,228);
- with at least 45 bb left to call, jam `QQ+ AKs AKo` (34) and fold the
  remainder (1,292). A raise of 45 bb is not an all-in at 200 bb, so the
  answer is to put the rest in rather than call and play three streets; when
  the opponent really is all-in there is nothing to raise and the jam becomes
  a call.

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
| BB re-raise versus a sub-6 bb raise | Greater of 10 bb or 4× the incoming size |
| SB re-raise versus a sub-6 bb raise | Greater of 12 bb or 3× the incoming size |
| Re-raise versus a 6-to-under-22 bb raise | Greater of 24 bb or 2.4× the incoming size |
| Raise versus 22 bb or more | All-in |

The direct action helper clamps the result into the harness's legal range and
handles short all-ins. Value and bluff labels currently use the same size; the
distinction is retained so later bots and reports can inspect the intended role.

## Public API

- `felt_preflop_class()` returns the canonical 169-class index.
- `felt_preflop_baseline_lookup()` exposes a pure hand-plus-spot lookup.
- `felt_preflop_baseline_decision()` uses history to recognize unopened and
  limped pots, then uses the additional amount to call to select a response
  bucket.
- `felt_preflop_baseline_action()` returns a legal `FeltAction` directly.
- `felt_preflop_action_count_v0_decision()` and
  `felt_preflop_action_count_v0_action()` expose the deliberately incorrect
  action-count variant.

All postflop bots should call the same direct helper preflop until this baseline
is intentionally versioned or replaced.

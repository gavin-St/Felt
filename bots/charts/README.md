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
| 6 bb to less than 16 bb | Versus a three-bet |
| 16 bb to less than 31 bb | Versus a four-bet |
| 31 bb to less than 50 bb | Versus a five-bet; conservative call/shove response |
| 50 bb or more | All-in-sized raise; tight jam-or-fold response |

The bands line up with the sizes the charts themselves raise to, so an
opponent playing the same charts lands in the next band along at each step.

An actual all-in is classified by the remaining call size too. This avoids
treating a short all-in like a 100 bb shove. For example, a first raise to 40
bb against a posted 1 bb blind costs 39 bb to call and uses the five-bet chart
even though it is technically only an opening raise. Conversely, a late raise
to 76 bb after the bot has already contributed 48 bb costs only 28 bb to call
and uses the four-bet chart.
If the chart marks a hand as a bluff re-raise but the opponent is already
all-in, that hand folds; a value re-raise falls back to calling.

## Responses derived from the supplied charts

The facing-raise charts are grids in `harness/src/preflop_chart.cpp` rather
than pattern lists, so what follows is a description of them; the source is the
record.

### Versus a three-bet, 6 to under 16 bb to call

- four-bet value: `AA KK QQ JJ TT AKs AKo AQs AQo AJs` (66 combos)
- four-bet bluff: `KJs Q3s Q2s 87s 76s 65s` (24)
- call: every remaining `Axs`, `Kxs`, and `Qxs` down to `Q4s`, `JTs` through
  `J7s`, `T9s T8s 98s 97s 86s 54s 43s 42s`, every pair below `TT`, and the
  broadway offsuit hands down to `A8o KTo QTo JTo J9o T9o` (348)
- fold everything else (888)

### Versus a four-bet, 16 to under 31 bb to call

- five-bet value: `AA KK QQ JJ AKs AKo` (40 combos)
- five-bet bluff: `AQo A5s A4s KJs KTs K9s K6s` (36)
- call: `AQs AJs ATs A9s KQs QJs QTs JTs TT T9s 99 88 87s 77 76s 66 65s 55
  54s` (88)
- fold everything else (1,162)

### Versus a five-bet, 31 to under 50 bb to call

- shove `QQ+ AKs AKo` (34 combos);
- call `JJ TT AQs AQo AJs AJo KQs T9s 98s 87s 76s 65s` (68);
- fold the remainder (1,224).

### Versus an all-in-sized raise, 50 bb or more to call

- jam `QQ+ AKs AKo` (34) and fold the remainder (1,292). A raise of 50 bb is
  not an all-in at 200 bb, so the answer is to put the rest in rather than call
  and play three streets; when the opponent really is all-in there is nothing
  to raise and the jam becomes a call.

## Deliberately incorrect comparison chart

`action_count_v0` preserves the earlier action-count routing as an experimental
control. It ignores raise size and treats the first raise as small, the second
as a three-bet, the third as a four-bet, and every later raise as a five-bet.
It never selects the all-in-sized bucket.

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
| Three-bet | 3.5× the incoming size |
| Four-bet | 3× the incoming size |
| Five-bet or beyond | 2× the incoming size |
| Raise with 31 bb or more left to call | All-in |

Openers are a fixed number of big blinds; every re-raise is a multiple of the
raise in front of it, and the multiple shrinks as the pot deepens. The count is
of voluntary raises already made, so our raise is the three-bet at one, the
four-bet at two, and a five-bet or beyond at three or more. From a 2.5 bb open
the ladder runs 2.5, 8.75, 26.25, 52.5, all-in.

The direct action helper clamps the result into the harness's legal range and
handles short all-ins, so a multiple past the stack simply becomes an all-in. Value and bluff labels currently use the same size; the
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

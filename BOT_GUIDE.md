# Writing a Felt bot

A bot is a strategy expressed as a pure function. Felt hands it a state, it
returns an action, and that is the entire relationship — no object, no
lifecycle hooks, no knowledge of the opponent, the score, or which hand this is.

This guide is the practical companion to [SPEC.md](SPEC.md), which is the
normative contract, and [GAME_RULES.md](GAME_RULES.md), which defines the poker.

## Quick start

```sh
cp -r templates/c_bot ~/my_bot && cd ~/my_bot
make FELT_INCLUDE=/path/to/felt/harness/include
run_match my_bot.dylib /path/to/felt/build/debug/bots/check_call.dylib \
  --hands 2000 --seed 1 --out ./results/smoke
```

Templates for C and C++, with Makefile and CMake builds, are in
[templates/](templates/). They already handle ABI checking, raise clamping
including the short all-in case, and the safe fallback action, so start there
rather than from a blank file.

## The three exports

```c
uint32_t     felt_bot_abi_version(void);   /* return FELT_BOT_ABI_VERSION */
const char  *felt_bot_name(void);          /* 1-127 bytes, lives for the process */
FeltAction   felt_bot_act(const FeltGameState *state);
```

Only the third is your strategy. The other two are one-liners you write once —
`return FELT_BOT_ABI_VERSION;` and `return "my-bot";` — so a complete working bot
is about twelve lines, as `bots/check_fold/check_fold.c` shows.

There is deliberately **no bot object and no create/destroy lifecycle**. A bot is
a strategy expressed as a function, not a thing that gets constructed. Per-hand
construction was dropped because it never enforced statelessness — globals and
function-local statics survive it — and keeping the boundary pure C avoids the
ABI fragility of virtual methods crossing `dlopen`.

The only build input is `harness/include/felt/bot_api.h`. There is nothing to
link against. A C++ bot must wrap these in `extern "C"` and must not let an
exception escape them.

Check `state->abi_version` and `state->struct_size` on entry and refuse anything
you do not recognise, rather than reading a struct laid out differently from the
one you compiled against.

## Reading the state

`FeltGameState` contains only what the acting player may legitimately see.

**Cards** are bytes `0..51` in rank-major order:

```c
rank = card >> 2   /* 0 = deuce ... 8 = ten, 9 = J, 10 = Q, 11 = K, 12 = A */
suit = card & 3    /* 0 = clubs, 1 = diamonds, 2 = spades, 3 = hearts */
```

So `2c == 0` and `Ah == 51`. `board[0 .. board_count)` is visible; the remaining
slots hold `FELT_INVALID_CARD` (255). Never read past `board_count`.

**Position** is `0` for the button/small blind and `1` for the big blind. Heads-up
reverses the usual order: the button posts the *small* blind and acts **first**
preflop, then **last** on every later street.

**Chips.** `pot` includes every chip committed so far, both players. `my_stack`
and `opp_stack` are what remains behind. `my_street_contribution` and
`opp_street_contribution` are what each has put in *on the current street* —
these are the numbers `amount_to` is denominated in. `to_call` is already capped
at your stack.

**History** is `state->history[0 .. history_count)`, covering this hand only.
Blind posts appear as explicit `FELT_EVENT_POST_SMALL_BLIND` and
`FELT_EVENT_POST_BIG_BLIND` events, so the preflop pot reconstructs correctly.
Each event carries the actor's position, the street, the event type, and their
total street contribution *after* the event.

The pointer is valid only for the duration of the call. Copy anything you need
to keep; never store the pointer.

## Returning an action

Four actions: `FELT_ACTION_FOLD`, `CHECK`, `CALL`, `RAISE_TO`.

**`legal_actions` is authoritative.** It is a bitmask built by the same code that
validates your reply, so testing it is exact. Do not infer legality from
`to_call` or the street.

**`amount_to` is a total, not an increment.** This is the mistake nearly every
new bot makes. `RAISE_TO.amount_to` is your total contribution *on the current
street*. If you have already put in 100 and want to make it 600, send 600, not
500. The field is ignored for fold, check and call — leave it zero.

A pot-sized raise is therefore:

```c
amount_to = my_street_contribution + to_call + (pot + to_call);
```

— call first, then bet the pot that results.

**All-in is not a separate action.** Use `RAISE_TO{max_raise_to}`, or `CALL` when
the call already consumes your stack.

**Short all-ins invert the bounds.** When the only raise available is an all-in
smaller than a full raise, Felt reports `max_raise_to < min_raise_to`, and the
single legal amount is exactly `max_raise_to`. Code that clamps up to
`min_raise_to` first will emit an illegal action here. Check the inversion
before clamping:

```c
if ((state->legal_actions & FELT_LEGAL_RAISE_TO) == 0) return /* can't raise */;
if (state->max_raise_to < state->min_raise_to) return state->max_raise_to;
/* otherwise clamp into [min_raise_to, max_raise_to] */
```

Otherwise any integer in the inclusive `[min_raise_to, max_raise_to]` range is
legal.

**Illegal actions are never silently fixed.** An illegal action or amount becomes
check if check is legal, otherwise fold, and a violation is recorded. Felt does
not clamp your bet size for you, because doing so would hide the bug.

## Randomness

Every decision receives an opaque `decision_random`. All randomized choices must
derive from it and from the state — no `rand()`, no clocks, no OS entropy, no
PRNG state carried between calls. Seed a small PRNG from it, as
`bots/seeded_random` does.

It is derived by domain-separated SHA-256 over the match seed, a randomness
index, the decision index and the acting position. Two consequences worth
knowing:

- It is **not** a seed you can run backwards into the deck. Neither the match
  nor deal seed is exposed.
- Both halves of a duplicate pair receive the **same** value for the same
  position and decision ordinal. Identical stateless bots therefore produce an
  exactly symmetric pair, which is what makes duplicate play cancel so much
  variance for randomized strategies.

## Timing

Two independent limits, doing different jobs.

**The CPU cap** (`--decision-cap-ms`, default 2 ms) is the fairness rule. It is
measured on your thread's own CPU clock *after* the call returns, so machine load
and scheduling are never charged to you. Exceeding it does not forfeit: your
action is replaced by check-or-fold and a violation is logged, which costs you
chips directly. It cannot interrupt you, only judge you afterwards.

**The hard timeout** (`--hard-timeout-ms`, default 1000 ms) is the liveness rule,
measured as wall time by the supervising parent process. If a decision never
returns, the match is killed and aborted with exit code 124. This one is
terminal — an infinite loop ends the whole match, not just one hand.

For a search bot, raise both together and keep the timeout well above the cap so
ordinary noise cannot trip it:

```sh
run_match a.dylib b.dylib --decision-cap-ms 500 --hard-timeout-ms 5000 --hands 3000
```

**Expensive initialization.** There is no init hook, so a large immutable table
is built on first use — inside a timed decision unless you arrange otherwise.
Use a function-local static in C++ or a lazily-filled file-scope table in C, and
if the cost matters, warm it from `felt_bot_name()`, which the harness calls once
at load time outside any decision.

## Statelessness

Each call must be a pure function of the state handed to you: no memory of
previous hands, no opponent modelling, no threads, no files. Felt does not
provide a hand index, the opponent's identity, previous results, or the score,
so there is nothing legitimate to accumulate.

This is a trusted contract, not an enforced one — v1 does not detect globals,
filesystem state or clocks. Breaking it does not get caught; it just makes your
results meaningless, because duplicate pairing assumes both halves see the same
strategy.

## Testing

Play against the reference bots first. Each finds a different class of bug:

| Opponent | What it exposes |
|---|---|
| `check_fold` | basic legality; you should beat it enormously |
| `check_call` | postflop lines and showdown handling |
| `always_all_in` | raise sizing, short all-ins, all-in calls — the fastest way to find sizing bugs |
| `seeded_random` | odd states you would not otherwise reach |
| `nit_all_in` | folding too much: shoves 1.4% of hands, so it bleeds blinds |
| `better_all_in` | shoving too much: 34.1% of hands, punishing loose calls |

Use a fixed `--seed` and a small `--hands` while iterating. Then check
`summary.json` for violation counts: **any nonzero illegal-action count is a bug
in your bot**, not a stylistic choice.

To inspect what actually happened, finalize and export, or read `hands.jsonl`
directly. `replay_match` re-runs logged actions through the engine without
calling bots; `rerun_match` re-runs your bot from the seed and may legitimately
differ if you changed the bot or sat on the timing boundary.

## Troubleshooting

**`cannot load bot '...': ...`** — `dlopen` failed. Usually the wrong
architecture (check `file my_bot.dylib` against your Mac), a missing dependent
library, or a path that is not actually a Mach-O dynamic library. Build with
`-dynamiclib`, not `-shared`.

**`cannot load symbol 'felt_bot_act' from '...'`** — the symbol is not exported.
In C++, you forgot `extern "C"`; check with
`nm -gU my_bot.dylib | grep felt_` and expect three unmangled names. If you build
with `-fvisibility=hidden`, the declarations in `bot_api.h` already carry the
export attribute, so include the header rather than declaring the functions
yourself.

**`bot '...' has ABI version N, expected M`** — you built against a different
`bot_api.h` than the harness. Rebuild against the harness you are running.

**`bot '...' returned a null name` / `name must contain 1 to 127 bytes`** —
`felt_bot_name()` returned null, an empty string, or something too long. It must
also outlive the call: return a string literal or a static buffer, never a stack
array.

**Load failures abort before the match starts** and are reported as setup errors.
They are deliberately *not* forfeits, so a broken build never lands a bogus
result in the ledger.

**Illegal-action violations.** Violation codes in the logs are `1` nonzero
reserved field, `2` unknown action type, `3` action illegal in this state, `4`
invalid raise amount, `5` CPU cap exceeded. Code `4` is almost always the
`amount_to` total-versus-increment confusion or the short all-in inversion. Code
`1` means you did not zero-initialize `FeltAction` — always start from `{0}`.

**Cap violations (code 5).** Your decision used more CPU than the cap. If they
cluster at the start of a match, it is first-call initialization: move the table
build into `felt_bot_name()`. If they are spread evenly, the strategy itself is
too slow — raise `--decision-cap-ms` or do less work.

**Exit code 124.** A decision exceeded the hard wall timeout and the match was
aborted. The message names the hand, decision, bot and street; `summary.json`
records `"status": "aborted"` with reason `decision_wall_timeout`. Look for an
unbounded loop or a wait on something external.

**Exit code 128 + N.** The worker died from signal N — `139` is a segfault
(`128 + 11`), typically reading past `board_count`, indexing history beyond
`history_count`, or using a stored `state` pointer after the call returned.
The match is aborted, not finalized.

**Results differ between runs.** Deals are reproducible from a seed; logs and
results are not guaranteed byte-identical. A bot sitting exactly on the cap
boundary can be accepted in one run and replaced in another, which changes the
hand. If results differ by more than that, the likely causes are hidden state,
uninitialized memory, or randomness not derived from `decision_random`. Run the
same seed twice and diff `hands.jsonl` to see where the lines first diverge.

**A match cannot be finalized.** `finalize_match.py` requires a complete summary.
An interrupted or aborted match stays readable for inspection but never enters
the ledger.

## Security

Version 1 runs bots as loaded code inside the match worker, with that process's
full privileges. There is no sandbox, no memory limit, and no filesystem or
network restriction; the supervisor guards liveness, not security. Only run
libraries you wrote or trust. Isolation for untrusted submissions is future work.

## Reference

- [SPEC.md](SPEC.md) — normative contract: API, timing, outputs, statistics
- [GAME_RULES.md](GAME_RULES.md) — poker rules, dealing, duplicate pairing, RNG
- [LOG_FORMAT.md](LOG_FORMAT.md) — match JSON and the SQLite ledger
- [templates/](templates/) — C and C++ starting points
- [bots/](bots/) — working reference bots

# Felt

A heads-up no-limit Hold'em harness for playing bots against each other, and for
measuring the result precisely enough to believe it.

Bots are trusted C dynamic libraries exposing three functions. A match plays
tens of thousands of hands in under ten minutes, applies duplicate dealing and
exact all-in equity adjustment to strip out luck, and lands in a local SQLite
ledger with per-hand history and per-bucket statistics you can query.

Felt plays the **ACPC heads-up format** — 50/100 blinds, equal 20,000-chip stacks
reset every hand ("Doyle's Game") — so results stay comparable to published
computer-poker work. See [PRIOR_ART.md](PRIOR_ART.md) for what else that
lineage settled, and where Felt deliberately departs from it.

**Version 1 does not sandbox bots.** A bot runs as loaded code inside the match
worker; only its wall time is supervised. Run libraries you trust and wrote. See
[Security boundary](#security-boundary).

## Status

| | Milestone | State |
|---|---|---|
| M0 | Build system and public C API | complete |
| M1 | Cards, seeded RNG, evaluator | complete |
| M2 | Betting engine | complete |
| M3 | Dealer and duplicate match runner | complete |
| M4 | Logging and replay | complete |
| M5 | Exact all-in equity | complete |
| M6 | Statistics | complete |
| M7 | Timing, performance, release | in progress |
| M8 | Round-robin ledger and hand index | in progress — ledger done, scheduling and Elo remain |
| M9 | Matrix and match-detail UI | not started |
| M10 | Documentation and bot onboarding | in progress |

Full sequence and exit criteria: [harness/PLAN.md](harness/PLAN.md).

## How a match works

Two `.dylib` bots are loaded with `dlopen` and called directly in-process — no
sockets, no serialization, so the overhead per decision is two clock reads and a
function call.

Hands are dealt in **duplicate pairs**: each deal is played twice with the bots
swapped between seats, so both bots face identical cards from both positions and
most of the card luck cancels. Default 20,000 hands is 10,000 such pairs.

When both players are all-in before the river, the pot is awarded by **exact
enumerated equity** rather than the dealt runout — every remaining board is
counted, integer wins and ties are stored, and the odd chip goes to the BB.
The real runout is still dealt and logged. Flop and turn all-ins enumerate live;
preflop matchups are memoized, and duplicate play guarantees each recurs.

Each decision is measured on both a **thread CPU clock** and a wall clock.
A decision over the CPU cap (default 2 ms) is replaced by the default action and
logged as a violation — self-punishing, since the bot loses its intended play.
Charging CPU rather than wall time is why Felt needs no time bank: machine load
is not billed to the bot.

## The bot API

A bot is a strategy expressed as a pure function. There is no bot object and no
lifecycle hook.

```c
uint32_t     felt_bot_abi_version(void);
const char  *felt_bot_name(void);
FeltAction   felt_bot_act(const FeltGameState *state);
```

`FeltGameState` carries only what the acting player may see: hole cards, the
board prefix, street, position, pot, both stacks, current-street contributions,
`to_call`, raise bounds, a legal-action bitmask, the full public history for this
hand, and an opaque `decision_random`.

Four actions: `FOLD`, `CHECK`, `CALL`, `RAISE_TO`. **`RAISE_TO.amount_to` is the
total contribution on the current street**, not additional chips — it covers both
opening bets and raises. All-in is not a separate action; use
`RAISE_TO{max_raise_to}`.

Bots must be pure, single-threaded, derive all randomness from
`decision_random`, and retain no pointers from the state. Illegal actions become
check-or-fold with a logged violation; amounts are never silently clamped,
because that would hide bot bugs.

Contract in full: [SPEC.md](SPEC.md). Poker and dealing rules:
[GAME_RULES.md](GAME_RULES.md). Reference bots — check-fold, check-call,
always-all-in, seeded-random — are in [bots/](bots/).

To write one, start from [BOT_GUIDE.md](BOT_GUIDE.md) and copy a template from
[templates/](templates/): they handle ABI checking, raise clamping including the
short all-in case, and the safe fallback action.

## Quick start

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Builds `run_match`, `replay_match` and `rerun_match`, plus the reference bots
under `build/debug/bots/`. Presets: `debug`, `release`, `asan-ubsan`.

Play a match:

```sh
run_match botA.dylib botB.dylib \
  --hands 20000 --seed 123 \
  --stack 20000 --sb 50 --bb 100 \
  --decision-cap-ms 2 \
  --out ./results/match-001
```

Duplicate play and equity adjustment are on by default; disable with
`--no-duplicate` / `--no-equity-adjust`. With duplicate on, `--hands` must be
even. A search-style bot can be given room with
`--decision-cap-ms 500 --hands 3000`.

Finalize into the ledger, then query or export:

```sh
./scripts/finalize_match.py results/match-001   # or a whole directory
./scripts/rebuild_stats.py                      # derived tables, from DB facts alone
./scripts/export_match.py MATCH_ID ./export     # reconstruct summary.json + hands.jsonl
replay_match ./export                           # re-run logged actions, verify terminal state
rerun_match ./export botA.dylib botB.dylib      # re-run bots from the seed (diagnostic)
```

Finalization is transactional: it imports the hand stream into the Git-ignored
`data/felt.sqlite3`, validates totals against the summary, computes statistics in
SQL, and only then removes the temporary stream. Statistics can always be rebuilt
from the database.

## What you get back

Per bot, per match: adjusted net chips and bb/100, raw wins/losses/chops, VPIP,
PFR, c-bet, WTSD, W$SD, all-in reached and initiated rates by street, showdown
and non-showdown winnings, per-street action frequencies, position splits, and
CPU/wall timing with violation counts — plus standard error computed from
**duplicate-pair** totals rather than pretending hands are independent.

Broken out by 169 starting-hand buckets and all 1,326 exact combos, each split by
position, and indexed one row per bot perspective per hand so a specific
situation can be found: bucket, exact cards, position, pot class (walk, limped,
single-raised, 3-bet, 4-bet+), flop seen, showdown or fold, all-in street and
initiator, final pot in BB, raw and adjusted outcome.

Schema and workflow: [LOG_FORMAT.md](LOG_FORMAT.md).

**On precision:** a 20,000-hand match resolves about 7 bb/100 at two sigma.
That is fine for "is this change an improvement" and too coarse for ranking
closely-matched bots — separating 1 bb/100 needs on the order of a million
hands. Ratings must carry uncertainty, never a point estimate. See
[elo/README.md](elo/README.md).

## Security boundary

Version 1 assumes **trusted** bots and is not a sandbox:

- bots are `dlopen`ed and called in-process, with full access to the harness;
- statelessness is a contract, not enforced — globals, files and clocks are not
  blocked;
- there is no filesystem, network, or syscall restriction, and no memory limit.

`run_match` forks a supervised worker, so a hung or crashing bot ends the match
with a recorded reason and a distinguishing exit code — `124` for a hard wall
timeout, `128 + N` for a signal — rather than hanging or silently losing the run.
An aborted match keeps its completed hands for inspection but cannot be
finalized. That is a liveness guard, not a security control. Isolation for
untrusted submissions is deferred work.

## Repository map

```
SPEC.md            contract: platform, bot API, timing, outputs, statistics
BOT_GUIDE.md       writing a bot: state, actions, timing, testing, troubleshooting
RELEASE_CHECKLIST.md  the four version numbers and when to bump each
GAME_RULES.md      poker, dealing, duplicate pairing, seed derivation
LOG_FORMAT.md      match JSON, SQLite schema, rebuild and export
PRIOR_ART.md       ACPC and MIT Pokerbots: what was taken, what was rejected
harness/           run_match, replay_match, rerun_match; engine, evaluator, logging
  PLAN.md            milestone sequence and exit criteria
  DESIGN_REVIEW.md   resolved decisions and remaining non-blockers
bots/              reference bots, each a trusted C dynamic library
templates/         copy-and-go C and C++ bot templates with build files
elo/               ratings ledger (later)
scripts/           finalization, statistics rebuild, export
data/              local SQLite ledger (Git-ignored, 10 GB v1 budget)
```

## Notes

Card evaluation vendors the evaluator-only portion of OMPEval under its ISC
license; the Release wrapper measured about 278 million seven-card evaluations
per second on an Apple M3 MacBook Pro, cross-checked against a brute-force
reference over 10 million hands.

Felt guarantees **reproducible deals from a seed**. It does not guarantee
byte-identical logs: timing is measured, and a bot sitting exactly on the cap
boundary may legitimately be accepted in one run and replaced in another.

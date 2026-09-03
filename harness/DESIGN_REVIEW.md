# Design Review — gaps to close before M0

**Verdict: not quite ready.** The big architectural calls are settled (child
processes, OMPEval, CPU-time cap, live equity memoization). What remains is a
layer of specification detail that looks minor and is not: most of it lands on
the **bot ABI**, which M3 freezes and which is expensive to change afterwards
because every bot ever written depends on it.

Items are marked **[BLOCKING]** if a wrong guess costs a rewrite, **[DECIDE]** if
it just needs a documented answer.

---

## A. Bot ABI — freeze these before writing a single header

### A1. Action amount semantics [BLOCKING]

**The single most common source of poker-bot bugs.** When a bot returns
`Raise{amount = 500}`, is 500 the *total* it wants its street contribution to
reach, or the *additional* chips on top of the current call?

The spec's `min_raise_to` / `max_raise_to` naming implies **raise-to**, but it is
never stated, and a bot author who assumes "additional" will produce legal-looking
actions that mean something else entirely — the harness will happily accept them.

**Recommendation:** raise-to, stated in a comment on the field itself, and name
the field `amount_to` rather than `amount` so the wrong reading is impossible.
Also state that `Bet` and `Raise` both use it, and that it is ignored for
`Fold`/`Check`/`Call`/`AllIn`.

### A2. Keep the boundary pure C [BLOCKING]

`Bot` is a C++ class with virtual methods loaded across `dlopen`. That works only
if the bot and harness agree on the C++ ABI. If `name()` returns `std::string`,
or `GameState` contains any `std::` type, a bot built against a different
libstdc++ will link fine and then corrupt memory at runtime — the worst possible
failure mode, because it looks like a poker bug.

**Recommendation:** nothing but POD crosses the boundary. `const char* name()`,
not `std::string`. `GameState` holds raw arrays and counts, no containers, no
spans with debug iterators. Add `static_assert(std::is_trivially_copyable_v<...>)`
and pin `sizeof`/`alignof` with static asserts so a layout change breaks the
build rather than the match.

### A3. Action history representation [BLOCKING]

Unspecified: the element type, whether blinds appear as actions, and the maximum
length. It has to be a fixed-size array in a POD struct, so the bound is load-bearing.

Derivation: every raise commits at least the 100-chip minimum increment, so from
a 20,000 stack there are at most **199 raise steps**, plus at most a couple of
checks/calls per street. **`MAX_ACTIONS = 256`** is safe with room to spare.

**Recommendation:** `{uint8 player, uint8 type, uint8 street, int32 amount_to}`,
fixed array of 256 plus a count. Blinds included as explicit posting actions —
a bot cannot otherwise reconstruct the preflop pot.

### A4. Sentinels when a raise is illegal [DECIDE]

What are `min_raise_to` and `max_raise_to` when the bot cannot raise — facing an
all-in for less, or holding too few chips? Zero and zero is ambiguous with a real
value. **Recommendation:** both set to 0, plus an explicit `bool can_raise`.
Cheap, and removes a class of off-by-one bugs.

### A5. `to_call` when it exceeds the stack [DECIDE]

Cannot arise with equal stacks, but the field should be defined anyway.
**Recommendation:** already capped at `my_stack`, asserted.

### A6. ABI version handshake [DECIDE]

M3 mentions a version constant but not its mechanism. **Recommendation:** the bot
exports `extern "C" uint32_t bot_abi_version()`; a missing symbol or a mismatch is
a load failure (see B1), not a forfeit.

---

## B. Failure handling

### B1. Load failures [DECIDE — already added to M3]

`dlopen` error, missing `create_bot`, ABI mismatch, `create_bot` returning null
or throwing. These are **setup errors, not gameplay violations** — they must
abort with a diagnostic rather than record a forfeit that pollutes the Elo
ledger. Distinct exit code.

### B2. SIGPIPE [DECIDE]

The parent writes to a child that has just died; default disposition kills the
parent. **Recommendation:** `signal(SIGPIPE, SIG_IGN)` at startup and handle
`EPIPE` explicitly. Easy to forget, and it presents as a mysterious harness crash.

### B3. Zombie reaping and shm cleanup [DECIDE]

`waitpid` every killed child. For shared memory, use an **anonymous `MAP_SHARED`
mapping created before `fork`** rather than a named POSIX segment — it is
inherited automatically and leaves nothing on the filesystem if the harness dies.

### B4. Forfeit mid-match and the duplicate pairing [BLOCKING]

If a bot forfeits at hand 25,001, the duplicate pair is half-finished and the
chip totals no longer reconcile. Reporting them as-is is silently wrong.

**Recommendation:** truncate to the last **complete duplicate pair**, mark the
match `forfeited` with the hand index in `summary.json`, and have the Elo tool
refuse forfeited and invalidated matches by default.

---

## C. Game engine

### C1. The big blind's option [BLOCKING]

When the button/SB *limps* (calls to 100), the BB must still get the chance to
check or raise. Skipping this is the classic heads-up engine bug and it silently
biases every result. Not currently in the spec.

**Recommendation:** explicit test case in M2, listed first.

### C2. Bet vs Raise leniency [DECIDE]

If a bot sends `Bet` when facing a bet (should be `Raise`), is that illegal?
Strict rejection turns a naming slip into a fold. **Recommendation:** treat
`Bet` and `Raise` as interchangeable and validate only the amount. Strict on
amounts, lenient on the type name.

### C3. Betting round termination [DECIDE]

State it precisely: the round ends when both players have acted since the last
aggressive action *and* contributions are equal, or when one is all-in and the
other has called or folded. Worth writing down because "has acted" is exactly
what the BB-option bug gets wrong.

### C4. Rounding rule for equity splits [DECIDE]

`pot * (2*wins + ties) / (2 * total)` needs a stated rounding direction, and the
remainder chip needs an owner. **Recommendation:** floor, remainder to the BB —
consistent with the odd-chip rule already settled, and cancelled by duplicate.

---

## D. Determinism

### D1. Timing data breaks byte-identical output [BLOCKING]

M4 requires "same seed → byte-identical output". M8 requires per-decision timing
in the hand log. **These contradict each other** — timing is inherently
non-deterministic.

**Recommendation:** split the outputs. `hands.jsonl` carries results only and is
byte-reproducible; `timing.jsonl` carries the measurements and is explicitly not.
The M4 determinism test compares only the former. Catching this after building
the logger would mean reworking the log schema and the replay tool together.

### D2. Other determinism leaks [DECIDE]

No timestamps, absolute paths, hostnames, or PIDs in the deterministic outputs.
No iteration over unordered containers where order reaches output. The integer
equity representation already keeps floating point out of the results path.

---

## E. Timing

### E1. Cold start [DECIDE]

The first decisions of a match pay page faults, cold caches, and lazy PLT
resolution — all of which count as CPU time and will trip a 2 ms cap through no
fault of the bot. **Recommendation:** call the bot on a few synthetic states
before the match begins, off the clock. Cheap, and it removes a whole class of
spurious violations.

### E2. Bots that spawn threads [DECIDE]

`CLOCK_THREAD_CPUTIME_ID` measures the calling thread only, so a bot could spawn
workers and use unlimited CPU. **Recommendation:** document that bots are
single-threaded, and measure `CLOCK_PROCESS_CPUTIME_ID` (subtracting the stub's
own negligible overhead) so the rule is enforced rather than trusted.

### E3. Cross-match state via the filesystem [DECIDE]

Nothing stops a bot writing a file and reading it next match. Only
`--fork-per-hand` plus a seccomp filter on file writes would prevent it.
**Recommendation:** do not attempt to prevent it; state plainly in the bot-author
guide that the statelessness guarantee is *within* a match, and that M9's replay
check is a sampled deterrent, not a proof.

---

## F. Statistics — pin the definitions before implementing

Ambiguous denominators are what make stats disagree between tools. Proposed,
to be written into the README verbatim:

| Stat | Definition |
|---|---|
| VPIP | voluntarily put chips in preflop (call or raise). Posting a blind does **not** count; the SB completing **does**. Denominator: hands dealt. |
| PFR | raised preflop. Denominator: hands dealt. |
| C-bet | bet on the flop as the preflop aggressor when first to act or checked to. Denominator: flops seen as preflop aggressor. |
| WTSD | reached showdown. Denominator: **hands that saw a flop**. |
| W$SD | won chips at showdown. Denominator: showdowns reached. |
| bb/100 | `chips_won / big_blind / hands * 100`. |

### F1. Raw vs adjusted in the stats [BLOCKING]

When equity adjustment fires, is the hand "won"? The adjusted result is
fractional — neither player won it. **Recommendation:** chip totals and bb/100
use **adjusted**; won/lost/chopped counts use the **actual dealt runout**, so the
counts stay integers and sum to hands played. State this in the README, because
the two will not reconcile against each other and someone will file that as a bug.

---

## G. Housekeeping

- **G1. OMPEval's license** — confirm it permits vendoring before committing it
  to `third_party/`, and record it in the repo.
- **G2. Output schema versioning** — put a `schema_version` in `summary.json` now.
  The Elo ledger will accumulate matches across harness versions.
- **G3. Bot log cap** — MIT bounds what a bot may write to disk (512 KB). We
  bound harness logging but not the bot's.
- **G4. `--hands` odd with duplicate on** — already in the decisions table; error
  out rather than silently truncating.

---

## Suggested order

1. **A1–A3** — the ABI freeze. Everything downstream depends on it.
2. **D1** — split the log files before writing the logger.
3. **C1, C3** — the BB option and round termination, as M2's first tests.
4. **F, B4** — before M7 accumulators exist.
5. The rest can be settled as their milestones arrive.

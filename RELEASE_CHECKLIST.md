# Release checklist

Felt has four version numbers that must move together with the things they
describe. They live in different files and in different languages, and nothing
in the build cross-checks them, so this list is the cross-check.

## The four versions

| Version | Where it lives | Covers |
|---|---|---|
| `FELT_BOT_ABI_VERSION` | `harness/include/felt/bot_api.h` | the binary bot interface |
| `kLogSchemaVersion` | `harness/src/match_log.cpp` | `summary.json` and `hands.jsonl` |
| `SCHEMA_VERSION` | `scripts/finalize_match.py` | the SQLite ledger |
| `kHarnessVersion` | `harness/src/match_log.cpp` | the build, recorded in every summary |

Current: ABI 1, log schema 2, database schema 1, harness `0.7.0-dev`.

## When to bump the bot ABI version

Bump `FELT_BOT_ABI_VERSION` when **anything a compiled bot could observe**
changes:

- any field added, removed, reordered, resized or retyped in `FeltGameState`,
  `FeltAction` or `FeltActionEvent` — including appending at the end;
- the numeric value of any `FELT_ACTION_*`, `FELT_LEGAL_*`, `FELT_STREET_*`,
  `FELT_POSITION_*` or `FELT_EVENT_*` constant;
- **the meaning of an existing field, even when no byte moves.** If
  `amount_to` ever changed from a total street contribution to an increment, the
  layout would be byte-identical and every existing bot would silently misbet.
  Semantics are part of the ABI;
- when a previously impossible value becomes possible, or a documented
  invariant is relaxed — for example if `to_call` stopped being capped at the
  acting player's stack.

Do **not** bump for: new constants nothing existing reads, comments, or changes
entirely inside the harness.

The check is exact equality, so a bump invalidates every prebuilt bot. That is
intended: rebuilding is cheap and a stale bot fails loudly at load rather than
misreading the struct. Rebuild every bot under `bots/` and both templates, then
confirm a deliberately stale `.dylib` is rejected with the expected message.

## When to bump the log schema version

Bump `kLogSchemaVersion` when a field is added, removed, renamed or given a new
meaning in `summary.json` or `hands.jsonl`. Both readers assert on it
(`match_log.cpp`), so `replay_match` and `rerun_match` will refuse older files
rather than misparse them.

Bumping it means `scripts/finalize_match.py` must learn the new shape in the same
change, since it ingests these files.

## When to bump the database schema version

Bump `SCHEMA_VERSION` in `scripts/finalize_match.py` for any change to tables,
columns, indexes or views, or to how derived statistics are computed. An existing
`data/felt.sqlite3` with a different value is rejected outright — there is no
migration path in v1, so a bump means the local ledger must be rebuilt from
exported matches or discarded deliberately.

Because derived tables rebuild from stored facts, a statistics-only change needs
`rebuild_stats.py` rather than a re-ingest — but it still needs the bump, or
stale derived rows will be silently mixed with new ones.

## Before tagging a release

1. **Versions.** Confirm each of the four is bumped if its surface changed, and
   that the four numbers quoted in this file still match the source.
2. **Docs follow the code.** `SPEC.md` is normative and must describe what the
   binary actually does. `BOT_GUIDE.md`, `templates/`, `GAME_RULES.md`,
   `LOG_FORMAT.md` and `README.md` must agree with it — the CLI flags and
   defaults in particular, which are quoted in several places.
3. **Templates build and pass.** Both templates compile clean under
   `-Wall -Wextra -Werror`, export exactly the three unmangled symbols, and play
   a short seeded match against `check_call` without violations.
4. **Reference bots rebuild** against the current header and load without ABI
   complaint. `bad_abi`, `missing_act` and `hanging` still produce their expected
   failures.
5. **Tests.** `ctest --preset debug` and `ctest --preset asan-ubsan` are green.
6. **End to end.** A seeded match finalizes into a scratch database, statistics
   rebuild from it, the match exports, and `replay_match` verifies it.
7. **Determinism.** The same seed produces the same deals. Timing fields and
   anything downstream of a cap-boundary decision may legitimately differ.
8. **Performance.** Record a default-configuration match's wall time on the
   reference Mac against the 5-10 minute target, plus evaluator throughput and
   decisions per hand.
9. **Version numbers in this file** updated to match what shipped.

## Known documentation gaps

- Statistics query examples against the ledger, and a database backup
  procedure — M10 item 3, waiting on the schema settling.
- Round-robin operation — M10 item 3, blocked until M8 scheduling exists.
- A submission guide for untrusted bots — M10 item 5, blocked until an isolated
  or Python runner exists. Until then the security boundary in
  [BOT_GUIDE.md](BOT_GUIDE.md) is the whole story: bots are trusted code.

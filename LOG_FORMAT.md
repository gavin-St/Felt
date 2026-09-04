# Match records and local database

Felt writes schema-versioned JSON while a match is running, then finalizes the
completed match into a local SQLite database. Match JSON uses schema version 2.

## Temporary match files

`run_match --out DIRECTORY` creates `summary.json` and `hands.jsonl`.

The summary is first written with `status: "running"` and a null result. It is
rewritten as `complete` only after all hand records are flushed and chip totals
reconcile. A supervisor timeout or worker crash instead changes the status to
`aborted`, leaves the result null, and adds the reason, completed-hand count,
active decision when known, hard timeout, and worker status. Only complete
summaries may be finalized. The summary also contains the harness version,
rules and seed, bot names, source paths and SHA-256 library hashes, plus complete
match results when available.

Each line of `hands.jsonl` is one complete played hand containing:

- hand, duplicate-pair, deal, and match identifiers;
- bot-to-position mapping, hole cards, board, and public history;
- every requested and applied action, public sizing field, violation, opaque
  decision randomness, and CPU and wall time;
- fold or showdown state, commitments, payouts, raw and adjusted results; and
- exact board, win, and tie counts for an adjusted pre-river all-in.

The base writer flushes every 64 hands; supervised `run_match` additionally
flushes after each completed hand before reporting progress to the parent. An
interrupted match remains recoverable for inspection but cannot be finalized
until its summary is complete.

Violation codes are `0` for none, `1` for a nonzero reserved field, `2` for an
unknown action, `3` for an action illegal in the current state, `4` for an
invalid raise amount, and `5` for exceeding the configured thread CPU cap. A
violation preserves the requested action but records the deterministic fallback
as the applied action.

## SQLite finalization

Finalize one match, or every not-yet-finalized match beneath a directory, with:

```text
./scripts/finalize_match.py MATCH_DIRECTORY
./scripts/finalize_match.py results/
```

The default database is `data/felt.sqlite3`. The finalizer performs one atomic
transaction:

1. insert the match, bots, rules profile, hands, player perspectives, and
   actions;
2. store the exact original JSON lines in independently compressed history
   chunks;
3. cross-check hand, chip, and position totals against `summary.json`;
4. build all derived statistics from the imported SQL facts; and
5. commit, then remove `hands.jsonl`, `hands.jsonl.gz`, and obsolete
   `stats.json` files.

A failure rolls back the match and leaves its source files in place. Use
`--keep-hand-log` for a diagnostic import or `--replace` to replace an identical
bot/configuration/seed match already in the ledger. SQLite files are local and
ignored by Git.

The principal fact tables are `matches`, `match_players`, `hands`,
`hand_players`, `actions`, and `hand_chunks`. Derived tables include
`match_bot_stats`, `position_stats`, `all_in_stats`, `pot_class_stats`,
`action_stats`, `timing_stats`, `hand_group_stats`, `pair_results`, and
`variance_stats`; `violation_stats` provides the violation-code breakdown.

`v_match_bot_stats` adds names, bb/100, and headline percentages.
`v_hand_group_stats` adds bucket/combo bb/100. For example:

```text
sqlite3 -header -column data/felt.sqlite3 \
  "SELECT * FROM v_match_bot_stats ORDER BY match_id, bot_slot;"
```

The perspective index supports filters for bot and opponent, match, starting
hand bucket, exact cards, position, pot class, flop/showdown status, final pot,
outcome, and all-in street. Its stable random key supports efficient random
matching-hand selection.

## Rebuilding and exporting

Derived tables can be deleted and rebuilt entirely from the database facts;
the external JSONL file is not needed:

```text
./scripts/rebuild_stats.py
./scripts/rebuild_stats.py MATCH_ID [MATCH_ID ...]
```

To inspect or replay a full history, reconstruct the original summary and hand
stream from compressed database chunks:

```text
./scripts/export_match.py MATCH_ID OUTPUT_DIRECTORY
replay_match OUTPUT_DIRECTORY
rerun_match OUTPUT_DIRECTORY BOT_A.dylib BOT_B.dylib
```

`--gzip` makes the exporter write `hands.jsonl.gz`; the replay tools accept
either form. `replay_match` feeds logged cards and applied actions through the
engine and verifies terminal state, equity counts, and aggregate totals without
calling bots. `rerun_match` regenerates the seeded match and calls the supplied
bots; it may legitimately differ if a bot changed, used external state, or ran
at a timing boundary.

# Match records and local database

Felt writes schema-versioned JSON while a match is running, then finalizes the
completed match into a local SQLite database. Match JSON and the current SQLite
schema use versions 2 and 3 respectively.

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

`v_match_bot_stats` adds names, bb/hand, and headline percentages.
`v_hand_group_stats` adds bucket/combo bb/hand.
`v_matrix_match_results` exposes two perspective rows per match with the bot,
opponent, raw and adjusted bb/hand, and duplicate-pair standard errors.

Useful queries include:

```text
sqlite3 -header -column data/felt.sqlite3 \
  "SELECT bot_name, opponent_name, adjusted_bb_per_hand,
          adjusted_standard_error, hand_count
   FROM v_matrix_match_results ORDER BY match_id, bot_id;"

sqlite3 -header -column data/felt.sqlite3 \
  "SELECT bot_name, group_key, hands,
          adjusted_net_chips * 1.0 / big_blind AS adjusted_bb,
          adjusted_bb_per_hand
   FROM v_hand_group_stats
   WHERE match_id = 1 AND bot_slot = 0 AND group_type = 'bucket'
         AND position = -1
   ORDER BY adjusted_bb DESC LIMIT 20;"

sqlite3 -header -column data/felt.sqlite3 \
  "SELECT b.name, t.decisions, t.mean_cpu_time_ns, t.p99_cpu_time_ns,
          t.max_cpu_time_ns, t.violations
   FROM timing_stats t JOIN match_players mp
     ON mp.match_id = t.match_id AND mp.bot_slot = t.bot_slot
   JOIN bots b ON b.id = mp.bot_id WHERE t.match_id = 1;"
```

The perspective index supports filters for bot and opponent, match, starting
hand bucket, exact cards, position, pot class, flop/showdown status, final pot,
outcome, and all-in street. Its stable random key supports efficient random
matching-hand selection.

Use `query_hands.py` for combined filters, cursor pagination, neighboring hands,
indexed random selection, and optional recovery of the full history:

```text
./scripts/query_hands.py --bot 1 --opponent 3 --bucket 76s \
  --pot-class three_bet --showdown yes --limit 50
./scripts/query_hands.py --after 2:9041:0 --limit 50
./scripts/query_hands.py --bot 1 --all-in-street preflop --random \
  --include-history
```

Bot IDs and names are listed with
`sqlite3 -header -column data/felt.sqlite3 "SELECT id,name,sha256 FROM bots;"`.

## Ratings and storage

Build the matrix ordering after matches are imported:

```text
./scripts/rebuild_ratings.py
./scripts/rebuild_ratings.py --profile 1
```

Version 1 expects one match for each unordered pair of bot hashes within a rules
profile. The rating build fails loudly on a repeated pairing instead of choosing
or combining results. Disconnected groups receive separate component numbers
and cannot be compared to one another.

The rating model gives each positive raw match result one logistic rating unit,
plus a margin bonus capped at 15%, then fits a zero-mean least-squares rating
graph on the standard Elo scale. Standard 20,000-hand matches receive equal
rating weight so a near-zero-variance opponent cannot dominate the ordering.
The reported 95%
intervals include an inflation factor when matchup results disagree with a
single transitive ordering, which is common for exploitable poker bots. Ratings
are stored in `ratings`; bot identity is the library SHA-256 hash.

Inspect current storage or project a batch of manually planned imports:

```text
./scripts/ledger_status.py
./scripts/ledger_status.py --additional-matches 20 --hands-per-match 20000
```

The estimate uses observed bytes per stored hand once enough hands exist and
warns above the 10 GB budget. It never deletes data.

## Backup

Use SQLite's online backup command so the copy is consistent even if the source
database is in WAL mode:

```text
mkdir -p backups
sqlite3 data/felt.sqlite3 ".backup 'backups/felt-2026-09-04.sqlite3'"
sqlite3 backups/felt-2026-09-04.sqlite3 "PRAGMA integrity_check;"
```

The integrity check must print `ok`. The backup contains normalized facts,
derived statistics, ratings, and compressed exact histories; the small result
directories are not needed to restore or export matches.

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

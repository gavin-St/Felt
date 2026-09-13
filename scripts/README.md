# Match and data scripts

Run these commands from the repository root. For normal match work, use
`match_workflow.py` rather than invoking the harness, importer, ratings builder,
and exporters separately.

## Match workflow

```sh
# New standard 20,000-hand match
./scripts/match_workflow.py play always-all-in check-call --seed 123

# Several new matches
./scripts/match_workflow.py batch \
  --match tilted-terry slp-fold 81001 \
  --match semi-bluff-sarah slp-fold 81002

# Preview and replace every existing match involving a changed bot
./scripts/match_workflow.py rerun --bot slp-balance --dry-run
./scripts/match_workflow.py rerun --bot slp-balance

# Other rerun selectors
./scripts/match_workflow.py rerun --match-id 42
./scripts/match_workflow.py rerun --prefix slp-
./scripts/match_workflow.py rerun --all

# Rebuild stats, ratings, and published web data from SQLite
./scripts/match_workflow.py refresh
```

`play` builds the requested bots, runs the match in a temporary directory,
validates it, imports every hand, calculates per-match statistics, rebuilds
ratings, and refreshes the dashboard. Raw JSONL is removed only after SQLite
commits. The command rejects an existing pairing and rejects a bot name already
attached to a different binary hash; use `rerun` for those cases.

`batch` runs one simulation at a time while one child process imports the
previous completed result. Its queue holds two completed runs by default; use
`--publish-queue-size` to change that. Ratings and the dashboard refresh once
after every match is imported. If simulation or publication fails, completed
matches remain valid and the command preserves its staging directory. Do not
run a second workflow command at the same time.

`rerun` preserves each selected match's ledger ID, hand count, seed, seats,
blinds, stack, timing cap, duplicate setting, equity adjustment setting, and
result directory. It refuses a partial rerun when a changed bot binary also
appears in matches outside the selected scope, preventing mixed bot versions in
one ledger. Each replacement publishes in its own SQLite transaction. An import
failure keeps that match's previous row and result directory, while replacements
completed before an interruption stay complete. Use `--decision-cap-us` only
when the selected matches should deliberately move to a new timing cap.

`refresh` recalculates all derived match statistics and ratings, then regenerates
the dashboard, per-match JSON, and sampled hand files. It builds the entire web
publication in a staging directory before replacing live files, so an
interrupted export leaves the previous publication intact.

The full-ledger SQLite integrity scan is off by default because it can take
several minutes on a large database. Add `--integrity-check` to `play`, `batch`,
`rerun`, or `refresh` when needed. Batch and rerun perform it once after all
imports. The script prints when the scan starts and when it passes.

Use `--keep-hand-logs` to retain raw JSONL. Use `--skip-build` only when the
release binaries are already current. `play --hard-timeout-ms N` overrides the
runner's derived wall timeout for a slow search bot. Run
`./scripts/match_workflow.py COMMAND --help` for all path and rule overrides.

## Storage

Small `summary.json` files under `results/` are tracked by Git. Full hand
histories and derived tables live in the Git-ignored `data/felt.sqlite3`. The
workflow also generates `web/data/dashboard.json`, `web/public/data/matches/`,
and the 200-hand samples under `web/public/data/hands/`.

Stop `hand_server.py` and other long-lived SQLite readers before a workflow
command. A reader can prevent WAL checkpointing while the ledger is updated.

## Lower-level tools

- `finalize_match.py` validates and imports raw match output. The workflow calls
  it automatically; use it directly for diagnostics or recovery.
- `rebuild_stats.py` recalculates derived per-match statistics from stored hands.
- `rebuild_ratings.py` rebuilds ratings from finalized match results.
- `query_hands.py` searches the local ledger for particular hand situations.
- `ledger_status.py` reports ledger coverage and match status.
- `hand_server.py` exposes the local read-only hand API used by the web app.
- `bootstrap_wasm.py` installs the pinned WebAssembly toolchain into a build
  directory.

Each tool supports `--help` for its exact arguments.

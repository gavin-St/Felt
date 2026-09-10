# Match results

This directory holds the small, Git-tracked `summary.json` artifact for each
match. Full hand histories and derived statistics live in the Git-ignored SQLite
ledger at `data/felt.sqlite3`.

Use the workflow command instead of manually running the harness, importer,
ratings builder, and dashboard exporter:

```sh
# New 20,000-hand match with the standard rules
./scripts/match_workflow.py play always-all-in check-call --seed 123

# Several new matches: serial simulation plus one background publisher
./scripts/match_workflow.py batch \
  --match tilted-terry slp-fold 81001 \
  --match semi-bluff-sarah slp-fold 81002

# Preview, then safely replace every existing result involving one changed bot
./scripts/match_workflow.py rerun --bot slp-balance --dry-run
./scripts/match_workflow.py rerun --bot slp-balance

# Replace exact matches or all existing matches involving an SLP bot
./scripts/match_workflow.py rerun --match-id 42
./scripts/match_workflow.py rerun --prefix slp-

# Replace the complete ledger; publish continuously and scan once at the end
./scripts/match_workflow.py rerun --all --decision-cap-us 200 --integrity-check

# Recalculate all derived tables, ratings, and web data from SQLite
./scripts/match_workflow.py refresh
```

`play` builds the requested bots, runs the match in a temporary directory,
validates it, imports all hands, calculates per-match statistics, rebuilds
ratings, refreshes `web/data/dashboard.json`, and removes the temporary raw
JSONL only after SQLite has committed. It rejects an existing pairing or a bot
binary whose name is already attached to a different hash; use `rerun` for
those cases.

`batch` is the faster path for several new matchups. The harness still runs only
one simulation at a time, while one child process publishes completed matches
from a bounded queue. The default queue holds at most two completed runs; change
that with `--publish-queue-size`. Ratings and the dashboard refresh once after
every match is safely imported, and the full-ledger integrity scan likewise runs
once at the end instead of once per match. If a simulation or publication fails,
already published matches remain valid and the command reports and preserves its
staging directory. Do not run a second workflow command alongside a batch.
The slow full-ledger scan is off by default. Pass `--integrity-check` when you
specifically want to run it after the workflow.

`rerun` keeps the original hand count, seed, seats, blinds, stack, timing cap,
duplicate setting, equity-adjustment setting, and result-directory name. It
stages and validates each selected match before replacing it. The next
simulation runs while one background publisher imports completed replacements,
with the same bounded queue as `batch`. If a bot's
binary changed, the command refuses a partial replacement that would leave two
versions of that bot in the ledger. Publication replaces one matchup per SQLite
transaction, so a failed import keeps that matchup's old database row and result
directory without copying the entire ledger. Global ratings, the dashboard, and
the optional integrity check run only after all replacements. Replacements completed before an
interruption remain complete; staged matches that have not started remain safe.
Use `--decision-cap-us` when a full rerun should deliberately move old matches
onto a new timing profile.

Pass `--keep-hand-logs` to retain JSONL beside a result. Pass `--skip-build` only
when the release binaries are already current. `play --hard-timeout-ms N` can
override the runner's derived wall timeout for a slow search bot. These commands
rerun existing ledger matchups; they do not create a round robin.

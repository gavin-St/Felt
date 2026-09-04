# Felt
macOS-first heads-up NLHE bot harness for trusted, stateless C bots.

- [SPEC.md](SPEC.md) — scope, bot API, timing, outputs, and statistics
- [GAME_RULES.md](GAME_RULES.md) — poker, dealing, duplicate, and RNG rules
- [PRIOR_ART.md](PRIOR_ART.md) — ACPC and MIT Pokerbots: what we take, what we reject
- [harness/PLAN.md](harness/PLAN.md) — implementation roadmap
- [harness/DESIGN_REVIEW.md](harness/DESIGN_REVIEW.md) — resolved decisions and remaining non-blockers

## Build

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The build produces the `run_match`, `replay_match`, and `rerun_match` tools plus
the reference bots under `build/debug/bots/`.

## Store a completed match

```sh
./scripts/finalize_match.py results/MATCH_DIRECTORY
```

This transactionally imports the hand stream into the local, Git-ignored
`data/felt.sqlite3`, validates it against the match summary, and calculates
indexed statistics from the imported rows. The original JSON hand stream is
stored in compressed database chunks and removed after a successful commit.

Recalculate statistics or reconstruct a replayable match with:

```sh
./scripts/rebuild_stats.py
./scripts/export_match.py MATCH_ID ./replay-export
```

See [LOG_FORMAT.md](LOG_FORMAT.md) for the tables, views, and verification
workflow.

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

## Generate statistics

```sh
./scripts/generate_stats.py results/MATCH_DIRECTORY
# Or regenerate every completed match under results/:
./scripts/generate_stats.py
```

This streams each authoritative `hands.jsonl`, verifies its totals against the
match summary, writes the derived `stats.json`, and compresses the hand stream
to `hands.jsonl.gz` by default. Raw hand logs in either form are ignored by Git;
summaries and compact statistics remain eligible to commit. Pass `--keep-jsonl`
to opt out of compression.

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

M0 builds a temporary `run_match` bot-loader probe and the reference bots under
`build/debug/bots/`. The executable becomes the full match runner in later
milestones.

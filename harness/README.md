# harness

The macOS `run_match` binary: game engine, direct dynamic-library bot calls,
timing measurement, duplicate dealing, equity adjustment, stats, and logging.

Version 1 assumes trusted bots and does not isolate them. A bot crash or hang
affects the match process.

Planned layout:

```
harness/
  CMakeLists.txt
  include/         # C-compatible bot API and value types
  src/             # engine, dealer, evaluator, timing, stats, logging, CLI
  third_party/     # OMPEval and any small vendored support code
  tests/
```

See [../SPEC.md](../SPEC.md), [../GAME_RULES.md](../GAME_RULES.md), and
[PLAN.md](PLAN.md).

# harness

The `run_match` binary: game engine, bot loading (`dlopen`), timing enforcement,
duplicate dealing, equity adjustment, stats and logging.

Planned layout:

```
harness/
  CMakeLists.txt
  include/         # public Bot interface (bot.h, game_state.h, action.h)
  src/             # engine, dealer, evaluator, timing, stats, logging, CLI
  third_party/     # OMPEval or 2+2 evaluator tables
  tests/
```

# bots

One subdirectory per bot. Each builds to a shared library exporting
`extern "C" Bot* create_bot()`.

Planned layout:

```
bots/
  always_fold/
  always_call/
  random/
  <your_bot>/
```

Bots must be stateless: `act(const GameState&)` is a pure function of its
argument, and any randomness must be derived from `GameState::rng_seed`.

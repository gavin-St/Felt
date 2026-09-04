# Bots

Each subdirectory contains a trusted C bot built as a macOS dynamic library.

Each has its own README with what it does, why it exists, and how it has
actually performed.

| Bot | One line |
|---|---|
| [`check_fold`](check_fold) | checks when free, folds to any bet |
| [`check_call`](check_call) | never folds, never raises |
| [`always_all_in`](always_all_in) | shoves every hand |
| [`seeded_random`](seeded_random) | uniform over the legal actions |
| [`nit_all_in`](nit_all_in) | shoves AA/KK/QQ only, 1.4% |
| [`better_all_in`](better_all_in) | shoves 99+, broadway, any ace, any king, 34.1% |
| [`worse_all_in`](worse_all_in) | shoves only junk — offsuit, disconnected, no ace or king, 32.6% |
| [`solved_all_in`](solved_all_in) | solved 200 bb shove-or-fold ranges |
| [`tests`](tests) | deliberately broken bots for the failure paths |

The all-in bots bracket the shoving spectrum: `nit_all_in` folds far too much,
`better_all_in` shoves far too much, `worse_all_in` shoves the wrong hands
entirely, and `solved_all_in` is the ceiling for the family. Beating that one
requires actually playing postflop.

`worse_all_in` and `better_all_in` are a controlled pair: near-identical
aggression frequency, disjoint ranges, so the gap between them is hand
selection alone.

A bot exports:

```c
uint32_t felt_bot_abi_version(void);
const char *felt_bot_name(void);
FeltAction felt_bot_act(const FeltGameState *state);
```

There is no bot object or lifecycle hook. The harness calls `felt_bot_act`
directly, so the normal overhead is only two clock reads plus one function call.

Bots must:

- treat each decision as a pure function of the supplied state;
- derive randomness only from `state->decision_random`;
- remain single-threaded;
- return raise sizes as total current-street contributions;
- never retain pointers from the state;
- never throw an exception through the C boundary if implemented in C++.

Version 1 does not sandbox bots. The supervising parent aborts a match whose
worker crashes or hangs, but the bot still has the worker's full privileges.
Only run libraries you trust.

A support library of strategy primitives — hand strength percentiles, made-hand
and draw classification, pot odds and history helpers — is planned in
[BOT_KIT.md](BOT_KIT.md).

To write your own, see [../BOT_GUIDE.md](../BOT_GUIDE.md) and the templates in
[../templates/](../templates/). The complete API contract is in
[../SPEC.md](../SPEC.md), and the poker rules in
[../GAME_RULES.md](../GAME_RULES.md).

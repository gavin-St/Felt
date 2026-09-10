# Bots

Each subdirectory contains a reference C bot built as a trusted macOS dynamic
library. Third-party C/C++ bots can instead be compiled to the `.wasm` format
documented in [../BOT_GUIDE.md](../BOT_GUIDE.md).

`slp` means **street-local policy**: these bots react to the current cards,
street, pot, and wager without interpreting the line taken on prior streets.

Each has its own README with what it does, why it exists, and how it has
actually performed.

| Bot | One line |
|---|---|
| [`check_fold`](check_fold) | checks when free, folds to any bet |
| [`check_call`](check_call) | never folds, never raises |
| [`always_all_in`](always_all_in) | shoves every hand |
| [`random_randy`](random_randy) | uniform over the legal actions |
| [`nit_all_in`](nit_all_in) | shoves AA/KK/QQ only, 1.4% |
| [`better_all_in`](better_all_in) | shoves 99+, broadway, any ace, any king, 34.1% |
| [`worse_all_in`](worse_all_in) | shoves only junk — offsuit, disconnected, no ace or king, 32.6% |
| [`solved_all_in`](solved_all_in) | solved 200 bb shove-or-fold ranges |
| [`slp_fold`](slp_fold) | chart preflop; value-bets strong hands and gives up with air |
| [`slp_bluff`](slp_bluff) | the same strategy, but bluffs every air hand |
| [`slp_balance`](slp_balance) | the same strategy, but bluffs 50% of air when checked to |
| [`slp_exploit_fold`](slp_exploit_fold) | attacks the fold profile, then respects its strength signal |
| [`slp_exploit_solved`](slp_exploit_solved) | open-min-raises every hand into the solved shove-or-fold bot |
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

Native libraries are not sandboxed. The supervising parent aborts a match whose
worker crashes or hangs, but a native bot still has the worker's full
privileges. Only run native libraries you trust; use the import-free Wasm runner
for third-party C/C++ artifacts.

A support library of strategy primitives is in progress. It currently provides
made-hand, live-draw, and board-texture classification, legal action helpers,
and a shared 100 bb heads-up preflop baseline; see [BOT_KIT.md](BOT_KIT.md) and
[charts/README.md](charts/README.md).

To write your own, see [../BOT_GUIDE.md](../BOT_GUIDE.md) and the templates in
[../templates/](../templates/). The poker rules are in
[../GAME_RULES.md](../GAME_RULES.md).

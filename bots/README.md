# Bots

Each subdirectory contains a trusted C bot built as a macOS dynamic library.

| Bot | Behaviour |
|---|---|
| `check_fold` | checks when free, folds to any bet |
| `check_call` | never folds, never raises |
| `always_all_in` | shoves every hand |
| `seeded_random` | uniform over legal actions, from `decision_random` |
| `nit_all_in` | shoves AA/KK/QQ only (18 combos, 1.4%), folds the rest |
| `better_all_in` | shoves 99+, any two broadway, any ace, any king (452 combos, 34.1%) |

The two all-in bots bracket the shoving spectrum: `nit_all_in` folds far too
much, `better_all_in` shoves far too much, and a sensible bot should beat both.

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

Version 1 does not sandbox bots or recover from their crashes and hangs. Only run
libraries you trust.

A support library of strategy primitives — hand strength percentiles, made-hand
and draw classification, pot odds and history helpers — is planned in
[BOT_KIT.md](BOT_KIT.md).

To write your own, see [../BOT_GUIDE.md](../BOT_GUIDE.md) and the templates in
[../templates/](../templates/). The complete API contract is in
[../SPEC.md](../SPEC.md), and the poker rules in
[../GAME_RULES.md](../GAME_RULES.md).

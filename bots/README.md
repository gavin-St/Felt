# Bots

Each subdirectory contains a trusted C bot built as a macOS dynamic library.
Reference bots include check-fold, check-call, always-all-in, and
seeded-random.

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

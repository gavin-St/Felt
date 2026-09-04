# tests

Deliberately broken bots, built only when `BUILD_TESTING` is on. Each exercises
one failure path the harness must handle as a *setup* error or a supervised
abort rather than as a gameplay result.

| Bot | Failure |
|---|---|
| `bad_abi` | reports the wrong `felt_bot_abi_version` |
| `missing_act` | omits the `felt_bot_act` symbol |
| `hanging` | never returns from a decision |

`bad_abi` and `missing_act` must abort before the match starts, with a clear
diagnostic and no forfeit recorded — a broken build should never land a result
in the ledger. `hanging` must trip the supervisor's wall-clock timeout and end
the match with exit code 124.

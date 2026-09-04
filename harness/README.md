# harness

The macOS `run_match` binary: game engine, direct dynamic-library bot calls,
timing measurement, duplicate dealing, equity adjustment, stats, and logging.

Version 1 assumes trusted bots and does not sandbox them. Bots run inside a
forked match worker; the parent survives a worker crash or hang and records the
match as aborted.

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

## Evaluator

Felt vendors the evaluator-only portion of OMPEval at commit
`4aec210ff75b0851af0ee170b35a7899e1a4fe8f` under its ISC license. Provenance
and the unchanged upstream license are in
[third_party/ompeval](third_party/ompeval/README.felt.md).

Felt and OMPEval both use rank-major card values. Their human-readable suit
orders differ, but no conversion is needed: poker evaluation depends on suit
equality, and a global permutation of the four suit labels preserves every hand
rank.

On an Apple M3 MacBook Pro, the Release evaluator wrapper processed about
278 million pre-generated random seven-card hands per second over a
100-million-evaluation benchmark. A separate Release run cross-checked
10 million random hands against the independent brute-force reference evaluator
in 16.2 seconds.

## Betting engine

`play_hand` owns all betting legality, action normalization, chip movement,
street progression, and raw showdown settlement for one hand. It accepts two
`BotRunner` instances by position, so tests use scripted runners without loading
dynamic libraries and the match runner can later supply native or Python-backed
runners through the same interface.

The engine records the state-facing sizing fields, requested and applied action,
and any violation for every decision. Scripted tests cover every street,
multi-raise and all-in sequences, short all-ins, illegal-action defaults,
effective stacks, showdown and chops. A further 10,000 generated legal hands
check termination, card visibility, legal-action agreement, payout
reconciliation, and zero-sum results across varied stack depths.

## Match runner

`run_match` now deals and plays complete matches. Duplicate mode is the default:
each adjacent pair reuses one positional deal while the two bot identities swap
positions. Without duplication, deals are fresh and the button alternates.
Every hand resets both stacks, and match totals are accumulated by bot and by
position with checked zero-sum reconciliation.

The command line supports `--hands`, `--seed`, `--stack`, `--sb`, `--bb`,
`--decision-cap-ms`, `--hard-timeout-ms`, `--no-duplicate`,
`--no-equity-adjust`, and `--out`.
It prints headline adjusted and retained raw chip totals and writes the detailed
match log. A call exceeding the configured thread CPU cap is logged and its
action becomes check when legal, otherwise fold. A supervising parent kills the
worker and aborts the match if a call exceeds the hard wall timeout.

By default, a called all-in before the river is settled for scoring from exact
enumerated equity while the seeded runout remains the raw result. Flop and turn
boards are enumerated directly; exact preflop matchups use a match-local lazy
cache shared across duplicate hands. Integer payout rounding assigns any
remainder to the big blind.

## Logging and verification

Each match writes a running-then-complete `summary.json` and streams one
authoritative record per hand to `hands.jsonl`, flushing every 64 hands. Bot
libraries are SHA-256 hashed. Decision records include requested and normalized
actions, violations, and measured thread CPU and wall time.

Run `./scripts/finalize_match.py MATCH_DIRECTORY` from the repository root to
import a completed match into the local `data/felt.sqlite3` ledger. It validates
aggregate totals, stores exact JSON histories in compressed chunks, builds
statistics from indexed SQL rows, commits atomically, and then removes the
temporary hand stream. Use `./scripts/rebuild_stats.py` to recalculate derived
statistics without reparsing external files.

Export a database match with `./scripts/export_match.py MATCH_ID OUTPUT_DIRECTORY`
before passing that directory to `replay_match` or `rerun_match`. Replay
reconstructs every hand without calling bots; rerun calls the supplied bots and
compares their fresh decisions. See [../LOG_FORMAT.md](../LOG_FORMAT.md).

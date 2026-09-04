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

# Design status

The v5 specification resolves the pre-code API and rules questions:

- macOS-first, trusted, in-process bots;
- a small C function ABI rather than a C++ object ABI;
- no bot lifecycle or persistent strategy state;
- four unambiguous actions with raise-to sizing;
- an explicit legal-action mask and public history format;
- adjacent positional duplicate pairs;
- pinned shuffle, dealing, and bot-randomness streams;
- observational timing logs with no byte-identical-output promise;
- exact-equity rounding and raw-versus-adjusted statistic definitions.

The remaining choices are not API blockers:

1. The reference Mac used to publish performance numbers.
2. Exact JSON/CSV field names within the already-settled schemas.
3. Whether the default CPU cap remains 2 ms after benchmarking.
4. Whether a preflop-equity cache should persist between matches.
5. Which uncertainty-aware model eventually replaces the placeholder Elo rule.

Do not add process isolation to v1 accidentally. It is a separate feature with a
different failure model and should be designed after the direct-call harness is
working and measured.

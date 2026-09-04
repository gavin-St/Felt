# Match log format

Felt writes schema-versioned JSON to the directory selected by `--out`. M5
uses schema version 2; version 1 was the raw-only M4 format.

## `summary.json`

The summary contains the harness version, `running` or `complete` status, the
complete match configuration, and each bot's reported name, source path, and
SHA-256 library hash. A complete summary contains both raw and equity-adjusted
net chips by bot and by position. Later milestones will add derived statistics
without changing the meaning of existing fields.

The file is first written with `status: "running"` and a null result. It is
rewritten as complete only after all hand records are flushed and chip totals
reconcile.

## `hands.jsonl` / `hands.jsonl.gz`

During a match, each line of `hands.jsonl` is one complete JSON object and one
authoritative played hand. Records
contain:

- hand, pair, deal, and match-seed identifiers plus the derived deal-seed hash;
- bot-to-position mapping, both hole-card pairs, and the full board;
- normalized public history events;
- every decision's public sizing fields and opaque `decision_random` value;
- the bot's requested action, applied action, violation code, thread CPU time,
  and wall time in nanoseconds;
- fold/showdown state, commitments, raw payouts, raw net chips, adjusted
  payouts, adjusted net chips, and showdown ranks;
- for a pre-river called all-in, exact board, win, and tie counts used for the
  adjustment; otherwise `equity` is null.

When adjustment is disabled or does not apply, adjusted payout and net fields
equal their raw counterparts. Timing values are observational and are
deliberately ignored when replaying a hand. The writer flushes after every 64
hands, bounding the output normally lost if a trusted bot crashes the process.

## `stats.json`

`stats.json` is derived rather than authoritative. Generate it for one match or
every match below `results/` with:

```text
./scripts/generate_stats.py MATCH_DIRECTORY
./scripts/generate_stats.py
```

The generator streams `hands.jsonl`, validates its hand and chip totals against
`summary.json`, atomically writes `stats.json`, and then compresses the hand log
to `hands.jsonl.gz` by default. Pass `--keep-jsonl` to retain plain JSONL. The
generator and verification commands transparently read either form. Statistics
include per-bot raw and
adjusted results, bb/100, position splits, outcome and poker rates, all-in and
pot-type breakdowns, actions, timings, violations, duplicate-pair uncertainty,
169 starting-hand buckets, and exact combinations. Rate objects carry the
count, denominator, fractional rate, and percentage so the UI does not need to
infer denominators.

## Verification commands

```text
replay_match OUTPUT_DIRECTORY
rerun_match OUTPUT_DIRECTORY BOT_A.dylib BOT_B.dylib
```

`replay_match` feeds the logged cards and applied actions through the betting
engine, recomputes any exact equity adjustment, and requires identical public
decisions, history events, terminal state, equity counts, and aggregate raw and
adjusted totals. It does not call the original bots.

`rerun_match` regenerates the match from its recorded seed and configuration,
calls the supplied bots, and reports action or outcome differences. Unlike
replay, rerun can legitimately differ if a bot changed, used forbidden external
state, or later operates at a timing boundary.

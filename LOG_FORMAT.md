# Match log format

Felt writes schema-versioned JSON to the directory selected by `--out`. M4
uses schema version 1.

## `summary.json`

The summary contains the harness version, `running` or `complete` status, the
complete match configuration, and each bot's reported name, source path, and
SHA-256 library hash. A complete summary also contains raw net chips by bot and
by position. Later milestones will extend the result with adjusted winnings and
statistics without changing the meaning of existing fields.

The file is first written with `status: "running"` and a null result. It is
rewritten as complete only after all hand records are flushed and chip totals
reconcile.

## `hands.jsonl`

Each line is one complete JSON object and one authoritative played hand. Records
contain:

- hand, pair, deal, and match-seed identifiers plus the derived deal-seed hash;
- bot-to-position mapping, both hole-card pairs, and the full board;
- normalized public history events;
- every decision's public sizing fields and opaque `decision_random` value;
- the bot's requested action, applied action, violation code, thread CPU time,
  and wall time in nanoseconds;
- fold/showdown state, commitments, raw payouts, raw net chips, and showdown
  ranks.

`adjusted_net` is null until exact all-in equity is implemented in M5. Timing
values are observational and are deliberately ignored when replaying a hand.
The writer flushes after every 64 hands, bounding the output normally lost if a
trusted bot crashes the process.

## Verification commands

```text
replay_match OUTPUT_DIRECTORY
rerun_match OUTPUT_DIRECTORY BOT_A.dylib BOT_B.dylib
```

`replay_match` feeds the logged cards and applied actions through the betting
engine and requires identical public decisions, history events, terminal state,
and aggregate totals. It does not call the original bots.

`rerun_match` regenerates the match from its recorded seed and configuration,
calls the supplied bots, and reports action or outcome differences. Unlike
replay, rerun can legitimately differ if a bot changed, used forbidden external
state, or later operates at a timing boundary.

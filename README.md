# Felt

### [Results dashboard](https://gavin-st.github.io/Felt/)

### [Submit a bot](BOT_GUIDE.md): open a PR or [upload a `.wasm` file](https://www.dropbox.com/request/1cjhkvn0jtbai2tcajrr)

Felt is a macOS-focused heads-up no-limit Hold'em harness for playing poker
bots against one another. Its C++ engine runs reproducible heads-up matches,
enforces per-decision time and compute limits, and records complete hand
histories and statistics in a local SQLite ledger.

Trusted C and C++ bots can run as native `.dylib` files. Third-party C/C++ bots
can instead be compiled to constrained WebAssembly modules with no host imports.

## Documentation

- [Game rules](GAME_RULES.md) — poker rules, stacks, duplicate dealing,
  randomness, timing, and result settlement.
- [Bot guide](BOT_GUIDE.md) — the bot API, C/C++ templates, native and
  WebAssembly builds, testing, and troubleshooting.
- [Log format](LOG_FORMAT.md) — match output, SQLite tables, statistics, and
  hand-history queries.
- [Prior art](PRIOR_ART.md) — the ACPC and Pokerbots ideas Felt builds on.

## Requirements

- macOS with a C/C++ compiler
- CMake 3.25 or newer
- Python 3
- Node.js 22.13 or newer for the web app

## Build and test

```sh
cmake --preset release
cmake --build --preset release
ctest --test-dir build/release --output-on-failure
```

This builds the harness and included bots under `build/release/`. Debug and
sanitizer presets are also available as `debug` and `asan-ubsan`.

## Add a bot

Copy either the C or C++ template, implement the three-function bot ABI, and
build it as a native library or WebAssembly module:

```sh
cp -r templates/c_bot bots/my_bot
make -C bots/my_bot
```

The complete walkthrough—including the visible game state, legal actions,
raise sizing, randomness, timing limits, and build commands—is in the
[bot guide](BOT_GUIDE.md). Existing implementations under [bots/](bots/) are
useful examples.

To enable WebAssembly compilation, install the pinned toolchain once:

```sh
./scripts/bootstrap_wasm.py --build-dir build/release
```

## Run matches

Use `match_workflow.py` instead of running the harness, importer, ratings
builder, and dashboard exporter separately. It builds the bots, validates and
imports each match transactionally, rebuilds ratings, refreshes the dashboard,
and removes raw JSONL only after SQLite commits.

Run and publish one standard 20,000-hand match:

```sh
./scripts/match_workflow.py play always-all-in check-call --seed 123
```

Run several new matchups with simulation and database publication pipelined:

```sh
./scripts/match_workflow.py batch \
  --match tilted-terry slp-fold 81001 \
  --match semi-bluff-sarah slp-fold 81002
```

`batch` runs one simulation at a time while one background process imports the
previous result. Do not start another workflow command while it is running.

After changing a bot, preview and replace its existing matchups:

```sh
./scripts/match_workflow.py rerun --bot slp-balance --dry-run
./scripts/match_workflow.py rerun --bot slp-balance
```

You can also select one ledger match with `rerun --match-id 42`, every bot whose
name starts with a prefix using `rerun --prefix slp-`, or the entire ledger with
`rerun --all`. A rerun preserves each match's ID, seed, hand count, seats,
blinds, stack, timing cap, and result directory. If a bot binary changed, the
workflow refuses a partial rerun that would mix versions of that bot.

Rebuild derived statistics, ratings, and all published web data without running
new hands:

```sh
./scripts/match_workflow.py refresh
```

The full SQLite integrity scan is off by default because it can take several
minutes on a large ledger. Add `--integrity-check` to any workflow command when
you specifically want it; for example, `rerun --all --integrity-check` runs it
once after replacing the complete ledger. Use `--keep-hand-logs` to retain raw
JSONL or `--skip-build` when release binaries are already current. Run
`./scripts/match_workflow.py COMMAND --help` for every option. See
[scripts/README.md](scripts/README.md) for publication and recovery details.

## Use the local database and web app

There is no database daemon to start. The match workflow creates the
Git-ignored SQLite file at `data/felt.sqlite3`. To browse its hands locally,
start the read-only API from the repository root:

```sh
python3 scripts/hand_server.py --database data/felt.sqlite3
```

Then start the web app in another terminal:

```sh
cd web
npm install
npm run dev
```

Open the local URL printed by the development server. Stop `hand_server.py`
before running a match workflow: a long-lived reader can prevent SQLite from
checkpointing while the ledger is being updated.

## Results and safety

Small match summaries are stored under `results/` and tracked by Git. Full hand
histories and derived data live only in `data/felt.sqlite3`, which is
intentionally ignored because it can grow to several gigabytes.

Native `.dylib` bots execute trusted code inside the match worker. Use `.wasm`
for outside submissions: Wasm bots receive no filesystem, network, clock,
process, or OS-randomness imports and are limited by memory, compute fuel, and
the harness watchdog. See the [bot guide](BOT_GUIDE.md#security) for the complete
boundary.

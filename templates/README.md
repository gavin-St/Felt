# Bot templates

Copy `c_bot/` or `cpp_bot/`, rename it, and write your strategy in
`choose_action()`. Both templates already handle the parts that are easy to get
wrong: ABI checking, raise clamping including the short all-in case, and the
safe fallback action.

## Build

From inside a copied template directory:

```sh
make FELT_INCLUDE=/path/to/felt/harness/include
```

or with CMake:

```sh
cmake -B build -DFELT_INCLUDE=/path/to/felt/harness/include
cmake --build build
```

or directly:

```sh
clang -O2 -std=c11 -fPIC -fvisibility=hidden \
  -I/path/to/felt/harness/include \
  -dynamiclib -o my_bot.dylib my_bot.c
```

Use `clang++ -std=c++17` and `my_bot.cpp` for the C++ template. The only build
input Felt needs is `harness/include/felt/bot_api.h`; there is nothing to link
against.

## Run

```sh
run_match my_bot.dylib /path/to/felt/build/debug/bots/check_call.dylib \
  --hands 2000 --seed 1 --out ./results/smoke
```

Start small and seeded. `check_fold`, `check_call`, `always_all_in` and
`seeded_random` make useful first opponents — `always_all_in` in particular will
find raise-sizing bugs immediately.

To add a bot to Felt's own build instead, drop it under `bots/` and add one line
to `bots/CMakeLists.txt`:

```cmake
add_felt_bot(my_bot my_bot/my_bot.c)
```

## Next

The full bot-author guide — reading the state, action sizing, randomness,
timing, testing, and troubleshooting by error message — is in
[../BOT_GUIDE.md](../BOT_GUIDE.md).

Four rules cause most first-bot bugs, so they are worth repeating here:

1. **`amount_to` is a total street contribution, not an increment.** To raise to
   600 having already put in 100, send 600.
2. **Short all-ins invert the raise bounds.** When the only raise is an all-in
   below a full raise, `max_raise_to < min_raise_to` and the one legal amount is
   exactly `max_raise_to`. Check that before clamping to the minimum.
3. **Randomness comes only from `decision_random`.** No `rand()`, no clocks.
4. **No exception may cross the `extern "C"` boundary.** It takes the match
   worker down.

Both templates already handle all four.

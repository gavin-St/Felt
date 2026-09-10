# One-command build. Override FELT_INCLUDE if Felt lives elsewhere.
FELT_INCLUDE ?= ../../harness/include
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -fPIC -fvisibility=hidden
WASI_SDK ?= ../../build/release/deps/wasi-sdk
WASM_ADAPTER ?= ../../harness/wasm/wasm_adapter.c
WASM_EXPORTS = \
	-Wl,--export-memory \
	-Wl,--export=felt_wasm_bot_abi_version \
	-Wl,--export=felt_wasm_state_ptr \
	-Wl,--export=felt_wasm_history_ptr \
	-Wl,--export=felt_wasm_history_capacity \
	-Wl,--export=felt_wasm_action_ptr \
	-Wl,--export=felt_wasm_bot_name_ptr \
	-Wl,--export=felt_wasm_act

my_bot.dylib: my_bot.c
	$(CC) $(CFLAGS) -I$(FELT_INCLUDE) -dynamiclib -o $@ $<

my_bot.wasm: my_bot.c $(WASM_ADAPTER)
	$(WASI_SDK)/bin/clang --target=wasm32-wasip1 -O2 -std=c11 \
		-mexec-model=reactor -I$(FELT_INCLUDE) $(WASM_EXPORTS) -o $@ $^

wasm: my_bot.wasm

clean:
	rm -f my_bot.dylib my_bot.wasm

.PHONY: clean wasm

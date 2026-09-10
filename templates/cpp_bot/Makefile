# One-command build. Override FELT_INCLUDE if Felt lives elsewhere.
FELT_INCLUDE ?= ../../harness/include
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -fPIC -fvisibility=hidden
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

my_bot.dylib: my_bot.cpp
	$(CXX) $(CXXFLAGS) -I$(FELT_INCLUDE) -dynamiclib -o $@ $<

my_bot.wasm: my_bot.cpp wasm_adapter.o
	$(WASI_SDK)/bin/clang++ --target=wasm32-wasip1 -O2 -std=c++17 \
		-mexec-model=reactor -fno-exceptions -fno-rtti -nostdlib++ \
		-DFELT_WASM_BUILD -I$(FELT_INCLUDE) $(WASM_EXPORTS) \
		-o $@ $^

wasm_adapter.o: $(WASM_ADAPTER)
	$(WASI_SDK)/bin/clang --target=wasm32-wasip1 -O2 -std=c11 \
		-I$(FELT_INCLUDE) -c -o $@ $<

wasm: my_bot.wasm

clean:
	rm -f my_bot.dylib my_bot.wasm wasm_adapter.o

.PHONY: clean wasm

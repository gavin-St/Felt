# One-command build. Override FELT_INCLUDE if Felt lives elsewhere.
FELT_INCLUDE ?= ../../harness/include
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -fPIC -fvisibility=hidden

my_bot.dylib: my_bot.cpp
	$(CXX) $(CXXFLAGS) -I$(FELT_INCLUDE) -dynamiclib -o $@ $<

clean:
	rm -f my_bot.dylib

.PHONY: clean

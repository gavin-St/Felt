# One-command build. Override FELT_INCLUDE if Felt lives elsewhere.
FELT_INCLUDE ?= ../../harness/include
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -fPIC -fvisibility=hidden

my_bot.dylib: my_bot.c
	$(CC) $(CFLAGS) -I$(FELT_INCLUDE) -dynamiclib -o $@ $<

clean:
	rm -f my_bot.dylib

.PHONY: clean

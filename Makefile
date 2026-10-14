CC = cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS += -D_POSIX_C_SOURCE=200809L $(shell $(PKG_CONFIG) --cflags libPlasma json-c)
LDLIBS += $(shell $(PKG_CONFIG) --libs libPlasma json-c)

.PHONY: all test clean
all: plasticine

plasticine: plasticine.c interop.c wire.c interop.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ plasticine.c interop.c wire.c $(LDLIBS)

plasticine-tests: tests.c interop.c wire.c interop.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests.c interop.c wire.c $(LDLIBS)

test: plasticine-tests
	./plasticine-tests

clean:
	rm -f plasticine plasticine-tests

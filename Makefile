CC = cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS += -D_POSIX_C_SOURCE=200809L $(shell $(PKG_CONFIG) --cflags libPlasma json-c)
LDLIBS += $(shell $(PKG_CONFIG) --libs libPlasma json-c)

.PHONY: all clean
all: plasma-export plasma-import

plasma-export: plasma-export.c interop.c wire.c interop.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ plasma-export.c interop.c wire.c $(LDLIBS)

plasma-import: plasma-import.c interop.c wire.c interop.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ plasma-import.c interop.c wire.c $(LDLIBS)

clean:
	rm -f plasma-export plasma-import

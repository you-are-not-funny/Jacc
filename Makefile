CC     ?= clang
PREFIX ?= /usr/local
CFLAGS ?= -O2 -std=c17 -Wall -Wextra

jacc: jacc.c
	$(CC) $(CFLAGS) -DJACC_DIR='"$(CURDIR)"' -o $@ jacc.c

test: jacc
	sh test.sh

bench: jacc
	sh bench.sh

install:
	$(CC) $(CFLAGS) -DJACC_DIR='"$(PREFIX)/share/jacc"' -o jacc.inst jacc.c
	install -Dm755 jacc.inst $(DESTDIR)$(PREFIX)/bin/jacc
	install -Dm644 jacc.h $(DESTDIR)$(PREFIX)/share/jacc/jacc.h
	install -Dm644 jacc_safe.h $(DESTDIR)$(PREFIX)/share/jacc/jacc_safe.h
	rm -f jacc.inst

clean:
	rm -f jacc jacc.inst

.PHONY: test bench install clean

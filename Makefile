CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra
LLVM    ?= /usr/lib/llvm-18

all: libjalloc.a passes

libjalloc.a: runtime/jalloc.o
	$(AR) rcs $@ $^

runtime/jalloc.o: runtime/jalloc.c runtime/jalloc.h runtime/lowfat.h
	$(CC) $(CFLAGS) -c -o $@ $<

passes:
	$(MAKE) -C passes LLVM=$(LLVM)

check: libjalloc.a passes
	$(CC) $(CFLAGS) -o tests/runtime_test tests/runtime_test.c libjalloc.a -lpthread
	./tests/runtime_test
	$(MAKE) -C passes check LLVM=$(LLVM)

clean:
	rm -f libjalloc.a runtime/*.o tests/runtime_test
	$(MAKE) -C passes clean

.PHONY: all passes check clean

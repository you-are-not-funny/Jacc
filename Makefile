CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra
LLVM    ?= /usr/lib/llvm-18

all: libjalloc.a passes

# interpose.o only gets linked in when the program uses malloc, which it
# always does; jalloc.o stays usable on its own for the unit tests
libjalloc.a: runtime/jalloc.o runtime/interpose.o
	$(AR) rcs $@ $^

runtime/%.o: runtime/%.c runtime/jalloc.h runtime/lowfat.h
	$(CC) $(CFLAGS) -c -o $@ $<

passes:
	$(MAKE) -C passes LLVM=$(LLVM)

check: libjalloc.a passes
	$(CC) $(CFLAGS) -o tests/runtime_test tests/runtime_test.c runtime/jalloc.o -lpthread
	./tests/runtime_test
	$(MAKE) -C passes check LLVM=$(LLVM)
	LLVM=$(LLVM) ./tests/e2e.sh

clean:
	rm -f libjalloc.a runtime/*.o tests/runtime_test
	$(MAKE) -C passes clean

.PHONY: all passes check clean

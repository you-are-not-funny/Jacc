LLVM ?= /usr/lib/llvm-18
CXX  ?= g++
CLANG = $(LLVM)/bin/clang

CXXFLAGS ?= -O2 -g -Wall -fno-rtti -fPIC -std=c++17
CXXFLAGS += $(shell $(LLVM)/bin/llvm-config --cxxflags | sed 's/-fno-exceptions//;s/-std=[^ ]*//;s/-O[0-9s]//;s/-g //')
LDFLAGS  += $(shell $(LLVM)/bin/llvm-config --ldflags)

OBJS = Instrument.o LoopWiden.o LowerChecks.o Plugin.o

all: libJacc.so

libJacc.so: $(OBJS)
	$(CXX) -shared -o $@ $(OBJS) $(LDFLAGS)

%.o: %.cpp JaccCommon.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

check: libJacc.so
	LLVM=$(LLVM) ./check.sh

clean:
	rm -f *.o libJacc.so

.PHONY: all check clean

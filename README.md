# Jacc

Just Another C Compiler. A memory-safe C front for LLVM that keeps the C
ABI: same pointer size, same struct layout, same calling convention, so
it links against objects built by GCC and Clang.

**Status: early prototype.** What exists today is the runtime allocator,
the bounds check, and two LLVM passes. It is not a compiler you can
build a system with yet. The goal and the plan are in `docs/SPEC.md`;
the overhead numbers there are targets, not measurements.

## What works now

| Piece | Where | Tested |
|---|---|---|
| Size-class allocator, double free and interior free trap, no address reuse | `runtime/jalloc.c` | `tests/runtime_test.c` |
| Bounds derived from the pointer value, no fat pointers | `runtime/lowfat.h` | same |
| Pass that inserts a check before loads/stores it can't prove safe | `passes/Instrument.cpp` | `passes/check.sh` |
| Pass that turns a per-iteration check into one range check before the loop | `passes/LoopWiden.cpp` | same |

Not done: the Clang frontend fork, stack object handling, the sweeper
that recycles freed memory, libc wrappers, integer overflow policy,
zero-init of locals, vectorizer integration. See the table at the end of
`docs/SPEC.md`.

## Known limits of what's here

- Bounds are the size *class*, not the size you asked for. `malloc(80)`
  lands in a 96-byte slot, so a write to byte 90 is not caught. Exact
  sizes need a side table; that's planned, not built.
- Freed addresses are never reused, so a use-after-free can't hit a new
  object. The price is address space: each size class has 32 GiB.
- Allocations over 16 MiB get their own mapping and are not checked.
- The loop pass assumes an inbounds GEP that wraps is undefined, which is
  what LLVM assumes too. A hoisted check can trap earlier than the
  original access would have. `-jacc-precise-traps` stops hoisting past
  calls.
- Tested on Linux x86-64 with LLVM 18 only.

## Build

Needs gcc or clang, make, and LLVM 18 dev packages
(`apt install llvm-18-dev clang-18`).

    make            # libjalloc.a and passes/libJacc.so
    make check      # runtime tests, then the pass tests

Use the passes with stock clang:

    clang-18 -O2 -fpass-plugin=passes/libJacc.so prog.c libjalloc.a -lpthread

Allocate with `jalloc_malloc` and friends. Pointers from the system
`malloc` are not in a checked region and pass every check unexamined.

## License

GPL-2.0-only, the same license as the Linux kernel. Full text in
`COPYING`.

In plain words: you can use, copy, change, and sell this, but if you
distribute a modified version you must hand the source of your changes
to the people you give it to, under the same license. That's how
improvements make their way back.

Programs you compile *with* Jacc are yours. The files in `runtime/` get a
linking exception (`runtime/EXCEPTION`) so a program linked against the
runtime doesn't have to be GPL.

There is **no warranty**. Sections 11 and 12 of `COPYING` say it in full
legal terms: the software is provided as is, and the authors aren't liable
for damage from using it. A safety tool that is unfinished can still miss
bugs, so don't treat it as a security boundary. This is a note, not legal
advice; if you plan to ship it commercially, have a lawyer read the license.

## Contributing

Patches welcome, see `CONTRIBUTING.md`. Short version: sign off your
commits (`git commit -s`) and your changes come in under GPL-2.0.

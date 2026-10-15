# Jacc

Just Another C Compiler. A memory-safe C front for LLVM that keeps the C
ABI: same pointer size, same struct layout, same calling convention, so
it links against objects built by GCC and Clang.

**Status: early prototype.** `bin/jacc` works as a drop-in front for
clang on small C programs: it adds bounds checks, defines the UB listed
below, and replaces malloc. It has not been run on real-world code bases
yet, so expect things to break. The plan is in `docs/SPEC.md`.

## What you get

Compile with `bin/jacc` the way you would with clang (`-O2`, `-c`, `-o`,
`-I` and so on all pass through):

- **Heap overruns trap.** malloc, calloc, realloc and friends are
  replaced process-wide, libraries included. Bounds come from the
  address; pointers stay 8 bytes.
- **Stack and global arrays are checked with exact sizes**, including
  VLAs.
- **`memcpy`, `memmove`, `memset`, `strncpy`, `strcpy`, `strcat` and
  struct copies are checked.**
- **Double free and free of a non-start pointer trap.** Freed memory is
  poisoned and its address never reused, so a stale pointer can't see a
  new object.
- **UB is defined:** signed overflow wraps (`-fjacc-int=trap` makes it
  trap), locals start at zero, strict aliasing is off, and a dereference
  doesn't let the optimizer assume non-null.
- **Loop checks are hoisted:** a loop over `a[i]` gets one range check
  before it instead of one per iteration, so it can still vectorize.

Tested by `make check`: the allocator, the passes on small loops, and 20
end-to-end programs at `-O0` and `-O2` (overruns trap, ordinary programs
using stdio, qsort and realloc keep working).

## Speed

One microbenchmark file (`bench/micro.c`), Linux x86-64, vs plain
`clang -O2` with glibc malloc, three runs:

| Kernel | Slowdown |
|---|---|
| streaming `y[i] += a*x[i]` | 1.0x |
| 400x400 matrix multiply | 1.0-1.3x |
| random gather `tab[idx[i]]` | 1.0-1.4x |
| malloc/free linked list | 2.3-3.0x |

This is four small kernels, not SPEC, and the run-to-run noise is big.
The list case is slow because freed memory is never recycled (more page
faults) and malloc takes a lock. The goal in the spec is single digits
percent on real code; nothing here shows that yet.

## Not done

Clang fork (we wrap clang instead), the sweeper that recycles freed
memory, libc checks beyond the string and mem functions above,
sub-object bounds, tag-bit hardware paths, checking of foreign code,
and everything in the last table of `docs/SPEC.md`.

## Known limits

- Heap bounds are the size *class*, not the size you asked for.
  `malloc(80)` gets a 96-byte slot, so a write to byte 90 isn't caught.
  Stack and global bounds are exact.
- Memory from anywhere but this allocator (mmap, a library's own pool)
  passes every check.
- Allocations over 16 MiB get their own mapping and are not checked.
- Freed addresses are never reused; each size class has 32 GiB.
- With `-fwrapv`, clang stops marking GEPs inbounds, so loop hoisting
  adds a trip-count guard; loops whose index needs int wraparound to
  analyze may keep their per-iteration check.
- LLVM may delete a malloc/free pair whose memory is never used, along
  with a double free on it.
- A hoisted check can trap earlier than the access would have.
  `-jacc-precise-traps` (set through `-mllvm`) stops hoisting past calls.
- Tested on Linux x86-64 with LLVM 18 only. Not a security boundary.

## Build

Needs gcc or clang, make, and LLVM 18 dev packages
(`apt install llvm-18-dev clang-18`).

    make            # libjalloc.a and passes/libJacc.so
    make check      # all tests

    bin/jacc -O2 -o prog prog.c
    bin/jacc -fjacc-int=trap -O2 -o prog prog.c    # overflow traps
    JACC_VERBOSE=1 bin/jacc -O2 -c prog.c          # why checks stay

Set `JACC_CC` to pick a different clang.

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

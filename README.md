# jacc

Just Another C Compiler. A small driver around clang that catches as many
bugs as it can at compile time, adds a few cheap runtime checks, and swaps the
worst libc calls for bounded ones, so ordinary C gets safer without being
rewritten.

Alpha. jacc is a wrapper: all code generation is clang/LLVM.

## Build

    make
    make test
    sudo make install

Needs clang and a POSIX system.

## Use

    jacc hello.c -o hello
    jacc --jacc-bounds hello.c -o hello     # also trap on bad array indexes
    jacc --jacc-fast hello.c -o hello       # fewer runtime checks, -O3
    jacc --jacc-strict hello.c -o hello     # strcpy/sprintf/gets are errors
    jacc --jacc-sanitize hello.c -o hello   # ASan + UBSan, for testing
    jacc --jacc-show hello.c                # print the clang command lines

Anything jacc doesn't know goes to clang. `JACC_CLANG` picks the binary.

## Compile time (most of the work)

Before compiling, jacc runs the clang static analyzer over every `.c` file
and fails the build on findings. It catches, among others: use after free,
double free, leaks, null dereference, out-of-bounds access on arrays and heap
blocks, returning stack addresses, string/memory function overruns and
overlapping copies.

On top of that the compiler turns a long list of warnings into errors:
implicit declarations, missing returns, int/pointer mixups, uninitialized use
(including "sometimes uninitialized"), constant out-of-range indexes, format
string mismatches, `sizeof` on pointers in memcpy-style calls, constant
division by zero and bad shifts.

The analyzer finds bugs; it does not prove their absence, and it can report
false positives. `--jacc-no-analyze` skips it. It adds a few tens of
milliseconds on small files; I haven't measured large projects, expect it to
cost noticeably more there.

## Your code stays the same

jacc force-includes `jacc_safe.h`, which rewrites these when the compiler can
see the buffer size (arrays, malloc'd blocks, -O1 or higher):

| you write | you get |
|---|---|
| `strcpy(buf, s)` | truncating, always NUL-terminated copy |
| `strcat(buf, s)` | truncating append |
| `sprintf(buf, ...)` | `snprintf` into the real buffer size |
| `vsprintf(buf, ...)` | `vsnprintf` into the real buffer size |
| `gets(buf)` | `fgets` + strip newline |

If the size isn't visible (a plain `char *` parameter) the original call is
left alone and glibc fortify still catches what it can. `gets` aborts then,
since there's nothing safe to fall back to. `tmpnam` and `mktemp` are always
errors.

## Runtime (kept small)

Always on: stack protector, stack-clash protection, `-fwrapv`, CET,
`_FORTIFY_SOURCE=3`, PIE, full RELRO, non-exec stack.

Default profile adds: trapping checks for division by zero, bad shifts,
unreachable code, null passed to `nonnull` params, bad VLA sizes; zeroed
locals; no removal of null checks.

Not on by default: per-access array bounds traps (`--jacc-bounds`). They are
the single biggest safety win and the biggest cost, which is why they're
opt-in. The analyzer covers many of the same bugs ahead of time, but only the
ones it can see on some path through the code.

## Cost

Measured on two small programs, best of 15 runs, on a noisy machine, so
treat as rough:

| profile | compute loop (bench.c) | malloc/libc heavy (bench_list.c) |
|---|---|---|
| plain `-O2` | baseline | baseline |
| default | about 0-2% | about 5-10% |
| `--jacc-bounds` | up to ~20% on tight array code | a few % |
| `--jacc-fast` | about 0-2% (and -O3) | about 0-5% |

So the 1-5% goal holds for some code and not for all. `make bench` on your
own workload before believing any of it.

## jacc.h

Also usable directly, `#include <jacc.h>`:

- `jacc_add / jacc_sub / jacc_mul(a, b, &out)` checked arithmetic, true if it fit
- `jacc_alloc(n, size)`, `jacc_grow(p, n, size)` overflow-checked, never NULL
- `jacc_copy`, `jacc_append` truncating string copy/append
- `jacc_wipe(p, n)` memset the optimizer can't drop
- `JACC_SLICE(T)`, `jacc_slice_of(arr)`, `jacc_at(s, i)` bounds-checked indexing

## Todo

- pointer bounds checking once `-fbounds-safety` is in release clang
- LTO and PGO profiles
- macOS and Windows

## Contributing

Patches welcome. Keep it small, plain C17 + POSIX, and add a case to
`test.sh` for anything new.

## License

MIT, see LICENSE.

# Jacc design notes

Goal: C that is memory safe and defined, at a cost close to plain C,
without changing the ABI. Honest numbers: nothing published does full
spatial plus temporal safety on SPEC under 3%. ASan is ~70%, software
SoftBound 60%+, the best hardware-assisted schemes (MTE, CHERI) 2-15%.
The target here is 5-12% on ordinary x86-64 and under 3% with tag
hardware. Those are targets. Nothing in this repo measures them yet.

## Pipeline

    C source -> Clang (forked, ABI lowering untouched)
             -> LLVM IR + check calls          passes/Instrument.cpp
             -> LLVM -O2, widen loop checks    passes/LoopWiden.cpp
             -> link with runtime/ (jalloc)

## 1. Frontend and ABI

Fork Clang: gets C23, GNU extensions, and target ABI for free. Nothing
changes before ABI lowering. Pointer size, layout, calling convention and
symbol names match GCC/Clang. Safety state is never stored next to the
pointer; it is recovered from the address, tag bits or a side table.

Unsafe idioms stay legal:

- Pointer arithmetic is allowed anywhere. Out of bounds is only an error
  when the pointer is used to access memory.
- Int to pointer casts keep working because bounds come from the address.
- Unions and type punning behave as in GCC.
- VLAs become alloca with the size overflow-checked and capped.
- `a[static n]`, `__counted_by`, `__sized_by` are used as bounds hints.
- `container_of` style code turns off sub-object narrowing for that type.

## 2. Spatial safety

Low-fat pointers. The heap is split into regions, one per size class.
The region number is in the address, so size and base come from a shift
and a multiply, no memory load beyond a small table:

    idx   = (p >> 35) - first_region
    start = base - (base mod size[idx])
    ok    = (p - start) <= size && n <= size - (p - start)

Checks are made against the *base the pointer came from*, not against the
pointer itself. Otherwise `p + size` is a valid pointer into the next
slot and looks fine. That is why the runtime call takes `(base, p, n)`.

Allocation size is `n + 1` so one-past-the-end is legal. This is also
why precision is the slot size and not the requested size (see README).

Sub-object narrowing (`&s->buf`) goes through a side table keyed by the
pointer's storage address, used only for pointers carrying a tag bit.
Not built.

Static bounds check elimination, cheapest first:

1. Stack and global objects with constant offsets: no check. (done)
2. Dominating check of the same base covering this one.
3. Interval and inequality-graph proofs from branch conditions.
4. Loop range checks via SCEV, one check before the loop. (done, see 5)
5. Whole-program: infer "this pointer must have n bytes" for callees and
   check at the call site instead.

## 3. Temporal safety

No tracing GC. Freed slots are poisoned and never handed out again, so a
dangling pointer can only ever see poison, never another object.
Double free is detected deterministically because the slot is still
marked freed. (done)

Memory is not reused yet. To reuse it safely we need a sweeper: when
quarantine gets big, scan heap, stacks and registers for words that land
in quarantined slots, and release only slots nobody points at. Type
bitmaps per allocation site shrink the scan; a static ownership pass can
skip quarantine entirely when a pointer provably has one owner. Known
holes: pointers hidden as integers, XOR lists, pointers only in kernel
buffers. Not built.

Stack: locals whose address escapes the frame move to the heap, with a
diagnostic. `return &local` is a compile error.

## 4. Undefined behavior

| Thing | Default | Strict mode |
|---|---|---|
| Signed overflow | wraps; nsw only where proven | trap |
| Shift >= width | count masked | trap |
| Divide by zero, INT_MIN / -1 | trap | same |
| Null dereference | trap; compiler may not infer non-null from a deref | same |
| Uninitialized locals | zero-filled; dead stores removed by the optimizer | poison pattern |
| Strict aliasing | off | opt-in |
| Evaluation order | fixed left to right | same |

Zeroing locals is legal because reading them was undefined to begin
with. Data races are out of scope.

## 5. Optimizer

Checks are calls to `__jacc_check` marked as touching only inaccessible
memory, so ordinary loads and stores can move across them but the call
itself can't be deleted. Passes run in this order:

    instrument -> LLVM simplify/inline/SROA/GVN -> rotate loops
               -> LoopSimplify -> LoopWiden -> vectorize -> lower

`LoopWiden` replaces `check(base, base + 8*i, 8)` inside a loop with one
`check_span(base, lo, hi, 8)` in the preheader when all hold:

- the address is affine in the loop induction variable (SCEV);
- the trip count is computable and the loop has one exit, the latch;
- the check dominates the latch, so it runs every iteration;
- no call in the loop can throw or fail to return;
- the access is through inbounds GEPs, so wrapping would be UB anyway.

If the address isn't affine (`a[idx[i]]`), the check stays in the loop.

Still to do: check coalescing after unrolling, vector-wide residual
checks so gathers can vectorize, versioning loops that fail the proof,
inliner awareness of surviving checks, nested loops (a hoisted check is
not yet hoisted again by the loop around it).

## 6. FFI

Trust tiers: compiled by Jacc (full checks), foreign with a contract
(libc wrappers check lengths), foreign without one, and explicit
`__jacc_unsafe` blocks.

Calls out pass plain pointers. Callbacks in work because bounds come from
addresses, not from metadata passed alongside. The allocator replaces
malloc process-wide so even uninstrumented libraries return pointers whose
bounds are recoverable. Memory the allocator doesn't own (mmap, foreign
globals) passes every check and is reported by a warning.
Foreign code can still corrupt checked memory; Jacc narrows the exposure
and makes it auditable, it doesn't remove it.

## 7. Trusting the optimizer

A check removed wrongly is the worst bug a tool like this can have.
Plan: every check-removing pass records its reason and a small separate
checker re-validates it in debug builds; differential testing against
ASan at -O0 and -O3; translation validation for the custom passes.

## Status of each piece

| Piece | State |
|---|---|
| Low-fat check, allocator, quarantine without reuse | done, tested |
| Check insertion with constant-offset stack/global elision | done, tested |
| Loop range check hoisting | done, tested on simple loops |
| Clang fork, stack handling, libc wrappers | not started |
| Sweeper and ownership inference | not started |
| Integer policy, zero-init | not started |
| Tag-bit (TBI/LAM) fast path, MTE | not started |
| Benchmarks | not started |

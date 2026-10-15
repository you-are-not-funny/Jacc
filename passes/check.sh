#!/bin/sh
# builds tests/loops.c with the plugin and looks at what survives
set -e
LLVM=${LLVM:-/usr/lib/llvm-18}
cd "$(dirname "$0")/.."
CC="$LLVM/bin/clang -O2 -fpass-plugin=$PWD/passes/libJacc.so"
$CC -S -emit-llvm tests/loops.c -o /tmp/jacc_loops.ll

per_fn() { awk -v f="$1" '$0 ~ "^define.* @"f"\\(" {p=1} p{print} p&&/^}/{exit}' /tmp/jacc_loops.ll; }

span=$(per_fn sum | grep -c '__jacc_check_span' || true)
chk=$(per_fn sum | grep -c '__jacc_fail' || true)
echo "sum: span=$span left-in-fn=$chk"
[ "$span" -ge 1 ] && [ "$chk" -eq 0 ]

span=$(per_fn fill_down | grep -c '__jacc_check_span' || true)
chk=$(per_fn fill_down | grep -c '__jacc_fail' || true)
echo "fill_down: span=$span left-in-fn=$chk"
[ "$span" -ge 1 ] && [ "$chk" -eq 0 ]

chk=$(per_fn gather | grep -c '__jacc_fail' || true)
echo "gather: left-in-fn=$chk (must stay > 0)"
[ "$chk" -gt 0 ]

# end to end: in-bounds runs, a loop that runs well past the object traps
$CC -o /tmp/jacc_loops tests/loops.c tests/loops_main.c libjalloc.a -lpthread
[ "$(/tmp/jacc_loops 10)" = 45 ]
if /tmp/jacc_loops 30 2>/dev/null; then echo "n=30 did not trap"; exit 1; fi
echo "passes ok"

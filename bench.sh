#!/bin/sh
# compares profiles on bench.c and bench_list.c, best of 7 runs
best() {
	b=999
	for i in 1 2 3 4 5 6 7; do
		s=$(date +%s.%N); "$1" >/dev/null; e=$(date +%s.%N)
		t=$(echo "$e - $s" | bc)
		b=$(echo "if ($t < $b) $t else $b" | bc)
	done
	echo "$b"
}
run() {
	name=$1; shift
	./jacc --jacc-no-analyze "$@" "$SRC" -o /tmp/jacc_bench 2>/dev/null || { echo "$name: build failed"; return; }
	printf "%-24s %ss\n" "$name" "$(best /tmp/jacc_bench)"
}
for SRC in bench.c bench_list.c; do
	echo "== $SRC"
	run "unsafe -O2"        --jacc-unsafe -O2
	run "default"
	run "default + bounds"  --jacc-bounds
	run "fast"              --jacc-fast
	run "fast + native"     --jacc-fast --jacc-native
done

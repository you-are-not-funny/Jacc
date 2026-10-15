#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Build each tests/e2e/*.c with bin/jacc at -O0 and -O2 and compare
# what happens against the "// expect:" line at the top.
#   expect: ok    exits 0
#   expect: trap  killed by a signal (the check, or the guard page)
# "// flags:" adds compiler options.

cd "$(dirname "$0")/.." || exit 2
fail=0
count=0

for src in tests/e2e/*.c; do
	want=$(sed -n 's|^// expect: *||p' "$src" | head -1)
	extra=$(sed -n 's|^// flags: *||p' "$src" | head -1)
	for opt in -O0 -O2; do
		exe=/tmp/jacc_e2e_$$
		if ! bin/jacc $opt $extra -o $exe "$src" 2>/tmp/jacc_e2e_err; then
			echo "FAIL $src $opt: does not compile"
			cat /tmp/jacc_e2e_err
			fail=$((fail + 1))
			continue
		fi
		$exe >/dev/null 2>&1
		rc=$?
		count=$((count + 1))
		case $want in
		ok)	[ $rc -eq 0 ] || { echo "FAIL $src $opt: exit $rc, wanted 0"; fail=$((fail + 1)); } ;;
		trap)	[ $rc -ge 128 ] || { echo "FAIL $src $opt: exit $rc, wanted a trap"; fail=$((fail + 1)); } ;;
		*)	echo "FAIL $src: no expect line"; fail=$((fail + 1)) ;;
		esac
		rm -f $exe
	done
done

echo "e2e: $count runs, $fail failed"
[ $fail -eq 0 ]

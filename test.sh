#!/bin/sh
# run from the repo root, after `make`
J=./jacc
T=$(mktemp -d) || exit 1
fail=0
ok()  { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }

# t NAME  - program text on stdin goes to $T/NAME.c
t() { cat > "$T/$1.c"; }

t hello <<'C'
#include <stdio.h>
int main(void) { puts("hello"); return 0; }
C
t implicit <<'C'
int main(void) { return foo(); }
C
t uninit <<'C'
#include <stdio.h>
int main(void) { int x; printf("%d\n", x); return 0; }
C
t copy <<'C'
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
	(void)argv;
	char b[8];
	const char *s = argc > 5 ? "x" : "much longer than eight";
	strcpy(b, s);
	puts(b);
	return 0;
}
C
t fmt <<'C'
#include <stdio.h>
int main(int argc, char **argv)
{
	(void)argv;
	char b[6];
	const char *s = argc > 5 ? "x" : "abcdefghij";
	sprintf(b, "%s", s);
	puts(b);
	return 0;
}
C
t getsline <<'C'
#include <stdio.h>
int main(void)
{
	char b[8];
	gets(b);
	puts(b);
	return 0;
}
C
t oob <<'C'
#include <stdio.h>
int main(int argc, char **argv)
{
	(void)argv;
	int a[4] = {1, 2, 3, 4};
	printf("%d\n", a[argc + 6]);
	return 0;
}
C
t lib <<'C'
#include <jacc.h>
#include <limits.h>
typedef JACC_SLICE(int) ints;
int main(void)
{
	int r;
	size_t sz;
	char buf[8];
	int arr[4] = {1, 2, 3, 4};
	ints s = jacc_slice_of(arr);

	if (jacc_add(INT_MAX, 1, &r)) return 1;
	if (!jacc_add(2, 3, &r) || r != 5) return 2;
	if (jacc_mul((size_t)-1, (size_t)2, &sz)) return 3;
	if (jacc_copy(buf, sizeof buf, "hello") != 5) return 4;
	if (jacc_append(buf, sizeof buf, " world") < sizeof buf) return 5;
	if (strcmp(buf, "hello w")) return 6;
	jacc_at(s, 3) = 9;
	if (arr[3] != 9) return 7;
	puts("lib ok");
	return 0;
}
C
t slice <<'C'
#include <jacc.h>
typedef JACC_SLICE(int) ints;
int main(int argc, char **argv)
{
	(void)argv;
	int arr[4] = {0};
	ints s = jacc_slice_of(arr);
	jacc_at(s, argc + 3) = 1;
	return 0;
}
C

t uaf <<'C'
#include <stdlib.h>
int main(void) { char *p = malloc(8); free(p); p[0] = 1; return 0; }
C
t dblfree <<'C'
#include <stdlib.h>
int main(void) { char *p = malloc(8); free(p); free(p); return 0; }
C
t leak <<'C'
#include <stdlib.h>
int main(void) { char *p = malloc(8); p[0] = 1; return 0; }
C
t nullderef <<'C'
#include <stdlib.h>
static int f(int *p) { if (!p) { } return *p; }
int main(void) { return f(NULL); }
C
t heapoob <<'C'
#include <stdlib.h>
int main(void) { int *a = malloc(4 * sizeof *a); a[4] = 1; free(a); return 0; }
C
t stackesc <<'C'
static int *f(void) { int x = 1; return &x; }
int main(void) { return *f(); }
C
t constoob <<'C'
int main(void) { int a[4]; a[4] = 1; return a[0]; }
C
t divzero <<'C'
int main(int argc, char **argv) { (void)argv; return 10 / (argc - 1); }
C

$J $T/hello.c -o $T/hello && [ "$($T/hello)" = hello ] && ok hello || bad hello
$J $T/lib.c -o $T/lib && [ "$($T/lib)" = "lib ok" ] && ok jacc.h || bad jacc.h

for f in implicit uninit; do
	$J $T/$f.c -o $T/x 2>/dev/null && bad "$f compiled" || ok "$f rejected"
done
$J --jacc-fast $T/implicit.c -o $T/x 2>/dev/null && bad "fast implicit" || ok "fast rejects implicit"

$J $T/copy.c -o $T/copy && [ "$($T/copy)" = "much lo" ] && ok "strcpy bounded" || bad "strcpy bounded"
$J $T/fmt.c -o $T/fmt && [ "$($T/fmt)" = "abcde" ] && ok "sprintf bounded" || bad "sprintf bounded"
$J $T/getsline.c -o $T/gl && [ "$(echo 'hello world, long line' | $T/gl)" = "hello w" ] && ok "gets bounded" || bad "gets bounded"
$J --jacc-strict $T/copy.c -o $T/x 2>/dev/null && bad "strict strcpy" || ok "strict rejects strcpy"

$J --jacc-no-analyze --jacc-bounds $T/oob.c -o $T/t && { $T/t >/dev/null 2>&1; [ $? -ne 0 ] && ok "oob traps with --jacc-bounds" || bad "oob ran clean"; } || bad "oob build"
$J --jacc-no-analyze $T/slice.c -o $T/t && { $T/t >/dev/null 2>&1; [ $? -ne 0 ] && ok "slice traps" || bad "slice ran clean"; } || bad "slice build"
$J $T/divzero.c -o $T/t && { $T/t >/dev/null 2>&1; [ $? -ne 0 ] && ok "div by zero traps" || bad "div ran clean"; } || bad "div build"

for f in uaf dblfree leak nullderef heapoob stackesc constoob oob slice; do
	$J $T/$f.c -o $T/x 2>/dev/null && bad "$f compiled" || ok "$f caught at compile time"
done
$J --jacc-no-analyze $T/uaf.c -o $T/x 2>/dev/null && ok "--jacc-no-analyze skips analysis" || bad "--jacc-no-analyze"
$J --jacc-fast --jacc-no-analyze $T/oob.c -o $T/x 2>/dev/null && ok "fast builds" || bad "fast builds"
$J --jacc-bogus $T/hello.c 2>/dev/null && bad "bogus option" || ok "bogus option rejected"

rm -rf "$T"
exit $fail

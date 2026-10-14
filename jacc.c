/* jacc: hardened driver for clang. MIT, see LICENSE. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define VERSION "0.6.0-alpha"

#ifndef JACC_DIR
#define JACC_DIR "."
#endif

/* compile time only */
static const char *diag[] = {
	"-std=c17", "-Wall", "-Wextra", "-Wpedantic", "-Wformat=2", "-Wshadow",
	"-Wconversion", "-Wvla", "-Wstrict-prototypes",
	"-Werror=implicit-function-declaration", "-Werror=implicit-int",
	"-Werror=return-type", "-Werror=int-conversion",
	"-Werror=incompatible-pointer-types", "-Werror=format-security",
	"-Werror=format", "-Werror=uninitialized",
	"-Werror=sometimes-uninitialized", "-Werror=array-bounds",
	"-Werror=fortify-source", "-Werror=sizeof-pointer-memaccess",
	"-Werror=division-by-zero", "-Werror=shift-count-negative",
	"-Werror=shift-count-overflow", "-Werror=return-stack-address",
};

/* cheap, always on */
static const char *cheap[] = {
	"-fstack-protector-strong", "-fstack-clash-protection",
	"-fno-strict-aliasing", "-fwrapv", "-fcf-protection=full",
	"-D_FORTIFY_SOURCE=3", "-fPIE", "-pie",
	"-Wl,-z,relro,-z,now,-z,noexecstack",
};

/* default runtime checks: one branch per operation that can fail, measured
 * at about 0-5% total. --jacc-fast drops them. */
static const char *runtime[] = {
	"-fsanitize=unreachable,integer-divide-by-zero,shift,vla-bound,"
	"nonnull-attribute,returns-nonnull-attribute,builtin",
	"-fsanitize-trap=unreachable,integer-divide-by-zero,shift,vla-bound,"
	"nonnull-attribute,returns-nonnull-attribute,builtin",
	"-ftrivial-auto-var-init=zero", "-fno-delete-null-pointer-checks",
};

/* per-access array checks: opt in with --jacc-bounds */
static const char *bounds[] = {
	"-fsanitize=bounds", "-fsanitize-trap=bounds",
};

/* clang static analyzer; findings fail the build */
static const char *checkers[] = {
	"alpha.security.ArrayBoundV2", "alpha.unix.cstring.OutOfBounds",
	"alpha.unix.cstring.BufferOverlap", "unix.MallocSizeof",
	"security.FloatLoopCounter",
};

struct vec {
	char **v;
	size_t n, cap;
};

static void push(struct vec *a, const char *s)
{
	if (a->n + 1 >= a->cap) {
		a->cap = a->cap ? a->cap * 2 : 64;
		a->v = realloc(a->v, a->cap * sizeof *a->v);
		if (!a->v) {
			perror("jacc");
			exit(1);
		}
	}
	a->v[a->n++] = (char *)s;
	a->v[a->n] = NULL;
}

#define PUSH_ALL(a, list) \
	for (size_t i_ = 0; i_ < sizeof(list) / sizeof(*(list)); i_++) \
		push((a), (list)[i_])

static int run(char **av)
{
	int st;
	pid_t p = fork();

	if (p < 0) {
		perror("jacc: fork");
		return -1;
	}
	if (p == 0) {
		execvp(av[0], av);
		perror("jacc: exec");
		_exit(127);
	}
	if (waitpid(p, &st, 0) < 0)
		return -1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int ends_with(const char *s, const char *suf)
{
	size_t n = strlen(s), m = strlen(suf);
	return n >= m && !strcmp(s + n - m, suf);
}

static int takes_value(const char *a)
{
	return !strcmp(a, "-I") || !strcmp(a, "-D") || !strcmp(a, "-U") ||
	       !strcmp(a, "-isystem") || !strcmp(a, "-include");
}

static void usage(void)
{
	puts("jacc " VERSION " - hardened C compiler driver (clang)\n"
	     "usage: jacc [options] file.c ... [-o out]\n\n"
	     "  --jacc-bounds       also trap on out-of-range array indexing (slower)\n"
	     "  --jacc-fast         -O3, cheap hardening only (no runtime traps)\n"
	     "  --jacc-no-analyze   skip the static analyzer pass\n"
	     "  --jacc-native       -march=native (binary won't be portable)\n"
	     "  --jacc-strict       make strcpy/sprintf/gets compile errors\n"
	     "                      instead of rewriting them to bounded versions\n"
	     "  --jacc-sanitize     add ASan + UBSan\n"
	     "  --jacc-plain-libc   don't force-include jacc_safe.h\n"
	     "  --jacc-unsafe       no hardening at all\n"
	     "  --jacc-show         print the clang command line\n"
	     "  --jacc-version\n\n"
	     "Everything else goes to clang. JACC_CLANG picks the clang binary.");
}

int main(int argc, char **argv)
{
	int fast = 0, native = 0, strict = 0, san = 0, plain = 0, unsafe = 0;
	int show = 0, bnd = 0, noan = 0, has_opt = 0, pre = 0, nsrc = 0;
	const char *clang = getenv("JACC_CLANG");
	struct vec out = { 0 }, an = { 0 };

	if (!clang || !*clang)
		clang = "clang";
	if (argc < 2) {
		usage();
		return 1;
	}

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage();
			return 0;
		} else if (!strcmp(a, "--jacc-version")) {
			puts("jacc " VERSION);
			return 0;
		} else if (!strcmp(a, "--jacc-fast")) fast = 1;
		else if (!strcmp(a, "--jacc-bounds")) bnd = 1;
		else if (!strcmp(a, "--jacc-native")) native = 1;
		else if (!strcmp(a, "--jacc-strict")) strict = 1;
		else if (!strcmp(a, "--jacc-sanitize")) san = 1;
		else if (!strcmp(a, "--jacc-plain-libc")) plain = 1;
		else if (!strcmp(a, "--jacc-no-analyze")) noan = 1;
		else if (!strcmp(a, "--jacc-unsafe")) unsafe = 1;
		else if (!strcmp(a, "--jacc-show")) show = 1;
		else if (!strncmp(a, "--jacc-", 7)) {
			fprintf(stderr, "jacc: unknown option %s\n", a);
			return 1;
		} else if (!strncmp(a, "-O", 2))
			has_opt = 1;
		else if (!strcmp(a, "-E"))
			pre = 1;
		else if (ends_with(a, ".c") && strcmp(argv[i - 1], "-o"))
			nsrc++;
	}

	push(&out, clang);
	push(&out, "-isystem");
	push(&out, JACC_DIR);

	if (!unsafe) {
		PUSH_ALL(&out, diag);
		PUSH_ALL(&out, cheap);
		if (!san && !fast)
			PUSH_ALL(&out, runtime);
		if (!san && bnd)
			PUSH_ALL(&out, bounds);
		/* fortify and object-size tracking need optimization */
		if (!has_opt)
			push(&out, fast ? "-O3" : "-O2");
		if (strict)
			push(&out, "-DJACC_STRICT");
		if (!plain) {
			push(&out, "-include");
			push(&out, "jacc_safe.h");
		}
	}
	if (native)
		push(&out, "-march=native");
	if (san)
		push(&out, "-fsanitize=address,undefined");

	for (int i = 1; i < argc; i++)
		if (strncmp(argv[i], "--jacc-", 7))
			push(&out, argv[i]);

	/* the analyzer sees the same includes and defines, not the codegen flags */
	if (!unsafe && !noan && !pre && nsrc) {
		push(&an, clang);
		push(&an, "--analyze");
		push(&an, "-w");
		push(&an, "-Xclang");
		push(&an, "-analyzer-werror");
		for (size_t k = 0; k < sizeof checkers / sizeof *checkers; k++) {
			size_t len = strlen(checkers[k]) + 20;
			char *c = malloc(len);
			if (!c) {
				perror("jacc");
				return 1;
			}
			snprintf(c, len, "-analyzer-checker=%s", checkers[k]);
			push(&an, "-Xclang");
			push(&an, c);
		}
		push(&an, "-isystem");
		push(&an, JACC_DIR);
		push(&an, "-std=c17");
		if (strict)
			push(&an, "-DJACC_STRICT");
		if (!plain) {
			push(&an, "-include");
			push(&an, "jacc_safe.h");
		}
		for (int i = 1; i < argc; i++) {
			const char *a = argv[i];
			if (!strncmp(a, "--jacc-", 7))
				continue;
			if (takes_value(a) && i + 1 < argc) {
				push(&an, a);
				push(&an, argv[++i]);
			} else if (!strncmp(a, "-I", 2) || !strncmp(a, "-D", 2) ||
				   !strncmp(a, "-U", 2) || !strncmp(a, "-std=", 5) ||
				   (ends_with(a, ".c") && strcmp(argv[i - 1], "-o")))
				push(&an, a);
		}
		push(&an, "-o");
		push(&an, "/dev/null");
	}

	if (show) {
		for (size_t i = 0; i < an.n; i++)
			printf("%s%c", an.v[i], i + 1 < an.n ? ' ' : '\n');
		for (size_t i = 0; i < out.n; i++)
			printf("%s%c", out.v[i], i + 1 < out.n ? ' ' : '\n');
	}

	if (an.n && run(an.v) != 0) {
		fprintf(stderr, "jacc: static analysis failed (--jacc-no-analyze to skip)\n");
		return 1;
	}

	execvp(clang, out.v);
	perror("jacc: exec clang");
	return 127;
}

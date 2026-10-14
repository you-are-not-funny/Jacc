// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../runtime/jalloc.h"
#include "../runtime/lowfat.h"

static int dies(void (*fn)(void))
{
	pid_t pid = fork();
	int st;

	if (!pid) {
		close(2);
		fn();
		_exit(0);
	}
	waitpid(pid, &st, 0);
	return WIFSIGNALED(st);
}

static void do_double_free(void)
{
	void *p = jalloc_malloc(40);
	jalloc_free(p);
	jalloc_free(p);
}

static void do_interior_free(void)
{
	char *p = jalloc_malloc(40);
	jalloc_free(p + 8);
}

static void do_oob(void)
{
	char *p = jalloc_malloc(40);
	__jacc_check(p, p + 40, 40);
}

static void do_span_oob(void)
{
	char *p = jalloc_malloc(100);
	__jacc_check_span(p, p, p + 200, 1);
}

/* the magic-multiply division must agree with the real thing */
static void test_division(void)
{
	unsigned long long s = 88172645463325252ULL;

	for (int c = 0; c < 144; c++) {
		if (!lf_size[c])
			continue;
		for (int i = 0; i < 200000; i++) {
			s ^= s << 13; s ^= s >> 7; s ^= s << 17;
			uint64_t rel = s & (LF_REGION - 1);
			uint64_t want = rel % lf_size[c];
			uint64_t got = rel - lf_mulhi(rel, lf_magic[c]) * lf_size[c];
			if (want != got) {
				printf("class %d size %llu rel %llu: want %llu got %llu\n",
				       c, (unsigned long long)lf_size[c],
				       (unsigned long long)rel,
				       (unsigned long long)want,
				       (unsigned long long)got);
				abort();
			}
		}
		/* worst cases: right around slot boundaries near the region end */
		for (uint64_t k = 0; k < 3; k++) {
			uint64_t top = (LF_REGION / lf_size[c] - 1 - k) * lf_size[c];
			for (uint64_t d = 0; d < lf_size[c] && d < 64; d++) {
				uint64_t rel = top + d;
				assert(rel - lf_mulhi(rel, lf_magic[c]) * lf_size[c] == d);
			}
		}
	}
}

int main(void)
{
	char *p, *q;

	p = jalloc_malloc(10);
	assert(p && jalloc_usable(p) >= 10);
	test_division();

	/* every legal byte, and one past the end, is in bounds */
	for (size_t n = 1; n < 5000; n += 7) {
		size_t u;

		p = jalloc_malloc(n);
		assert(p);
		u = jalloc_usable(p);
		assert(lf_check(p, p, n));
		assert(lf_check(p, p + n, 0));
		assert(lf_check(p, p + n - 1, 1));
		assert(!lf_check(p, p, u + 2));
		assert(!lf_check(p, p + u, 2));
		/* the neighbour trick: p+u+1 is a fine pointer in the next
		 * slot, but not a fine access relative to p */
		assert(!lf_check(p, p + u + 1, 1));
		assert(!lf_check(p, p - 1, 1));
		memset(p, 1, n);
	}

	p = jalloc_malloc(48);
	q = jalloc_malloc(48);
	assert(lf_check_span(p, p, p + 40, 8) == 1);
	assert(lf_check_span(p, p, q, 8) == 0);
	assert(lf_check_span(p, p + 8, p, 8) == 0);

	/* zeroed, and realloc keeps contents */
	p = jalloc_calloc(100, 4);
	for (int i = 0; i < 400; i++)
		assert(p[i] == 0);
	strcpy(p, "hello");
	p = jalloc_realloc(p, 100000);
	assert(!strcmp(p, "hello"));
	jalloc_free(p);

	/* big allocations are unchecked but work */
	p = jalloc_malloc(64UL << 20);
	assert(p && lf_check(p, p, 1UL << 30));
	memset(p, 7, 64UL << 20);
	jalloc_free(p);

	assert(jalloc_calloc(~0UL, 2) == NULL);

	assert(dies(do_double_free));
	assert(dies(do_interior_free));
	assert(dies(do_oob));
	assert(dies(do_span_oob));

	puts("runtime ok");
	return 0;
}

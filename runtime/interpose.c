// SPDX-License-Identifier: GPL-2.0-only WITH Jacc-runtime-exception
/*
 * Takes over malloc and friends, so unmodified programs and every
 * library they pull in get memory with recoverable bounds.
 *
 * This is its own object file inside libjalloc.a: the linker only
 * pulls it in when something references malloc, which is always.
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "jalloc.h"

#define PAGE 4096UL

void *malloc(size_t n)             { return jalloc_malloc(n); }
void  free(void *p)                { jalloc_free(p); }
void *calloc(size_t n, size_t sz)  { return jalloc_calloc(n, sz); }
void *realloc(void *p, size_t n)   { return jalloc_realloc(p, n); }
void *memalign(size_t a, size_t n) { return jalloc_memalign(a, n); }
size_t malloc_usable_size(void *p) { return p ? jalloc_usable(p) : 0; }

void *aligned_alloc(size_t a, size_t n)
{
	return jalloc_memalign(a, n);
}

int posix_memalign(void **out, size_t a, size_t n)
{
	void *p;

	if (a < sizeof(void *) || (a & (a - 1)))
		return EINVAL;
	p = jalloc_memalign(a, n);
	if (!p)
		return ENOMEM;
	*out = p;
	return 0;
}

void *valloc(size_t n)
{
	return jalloc_memalign(PAGE, n);
}

void *pvalloc(size_t n)
{
	return jalloc_memalign(PAGE, (n + PAGE - 1) & ~(PAGE - 1));
}

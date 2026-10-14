/* jacc.h - small helpers for writing C that fails loudly instead of quietly.
 * Header-only, no dependencies beyond libc. Included with -I by jacc. */
#ifndef JACC_H
#define JACC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Checked arithmetic. Each returns true when the result fit and was stored
 * in *out, false on overflow (*out then holds the wrapped value). Works for
 * any integer types. */
#define jacc_add(a, b, out) (!__builtin_add_overflow((a), (b), (out)))
#define jacc_sub(a, b, out) (!__builtin_sub_overflow((a), (b), (out)))
#define jacc_mul(a, b, out) (!__builtin_mul_overflow((a), (b), (out)))

static inline void jacc_die(const char *msg)
{
	fprintf(stderr, "jacc: %s\n", msg);
	abort();
}

/* Allocation that either works or kills the process. The size product is
 * checked, so jacc_alloc(n, sizeof(T)) can't wrap. Memory is zeroed. */
static inline void *jacc_alloc(size_t n, size_t size)
{
	void *p = calloc(n, size);
	if (!p && n && size)
		jacc_die("out of memory");
	return p;
}

static inline void *jacc_grow(void *p, size_t n, size_t size)
{
	size_t total;
	if (!jacc_mul(n, size, &total))
		jacc_die("allocation size overflow");
	void *q = realloc(p, total);
	if (!q && total)
		jacc_die("out of memory");
	return q;
}

/* strlcpy-style copy: always terminates when cap > 0, returns strlen(src).
 * Truncated if the return value is >= cap. */
static inline size_t jacc_copy(char *dst, size_t cap, const char *src)
{
	size_t len = strlen(src);
	if (cap) {
		size_t n = len < cap - 1 ? len : cap - 1;
		memcpy(dst, src, n);
		dst[n] = '\0';
	}
	return len;
}

/* strlcat-style append. Same truncation rule as jacc_copy. */
static inline size_t jacc_append(char *dst, size_t cap, const char *src)
{
	const char *nul = memchr(dst, '\0', cap);
	if (!nul)
		return cap + strlen(src);
	size_t used = (size_t)(nul - dst);
	return used + jacc_copy(dst + used, cap - used, src);
}

/* memset that the optimizer is not allowed to drop. For keys and such. */
static inline void jacc_wipe(void *p, size_t n)
{
	volatile unsigned char *v = p;
	while (n--)
		*v++ = 0;
}

/* Slices: pointer plus length, indexed with a bounds check that traps.
 *
 *   typedef JACC_SLICE(int) ints;
 *   int buf[16];
 *   ints s = jacc_slice_of(buf);
 *   jacc_at(s, 3) = 7;
 *
 * jacc_at evaluates its slice argument twice, so keep it side-effect free. */
#define JACC_SLICE(T) struct { T *ptr; size_t len; }
#define jacc_slice_of(a) { (a), sizeof(a) / sizeof((a)[0]) }

static inline size_t jacc_idx(size_t i, size_t len)
{
	if (i >= len)
		__builtin_trap();
	return i;
}
#define jacc_at(s, i) ((s).ptr[jacc_idx((size_t)(i), (s).len)])

#endif

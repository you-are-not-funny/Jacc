/* SPDX-License-Identifier: GPL-2.0-only WITH Jacc-runtime-exception */
/*
 * Low-fat bounds: an object's size and base are recovered from its
 * address alone. The heap is one big reservation cut into regions,
 * one region per size class, so (addr >> LF_SHIFT) picks the class.
 */
#ifndef JACC_LOWFAT_H
#define JACC_LOWFAT_H

#include <stddef.h>
#include <stdint.h>

#define LF_SHIFT   35
#define LF_REGION  (1ULL << LF_SHIFT)
#define LF_NREG    144

extern uint64_t lf_first;           /* (heap base >> LF_SHIFT) */
extern uint64_t lf_size[LF_NREG];   /* 0 means "not ours, don't check" */
extern uint64_t lf_magic[LF_NREG];  /* floor((2^64-1)/size) + 1 */

static inline uint64_t lf_mulhi(uint64_t a, uint64_t b)
{
	return (uint64_t)(((unsigned __int128)a * b) >> 64);
}

static inline uint64_t lf_region(const void *p)
{
	return ((uint64_t)p >> LF_SHIFT) - lf_first;
}

/* offset of p inside its slot; only valid when lf_size[idx] != 0 */
static inline uint64_t lf_offset(uint64_t p, uint64_t idx)
{
	uint64_t rel = p & (LF_REGION - 1);
	return rel - lf_mulhi(rel, lf_magic[idx]) * lf_size[idx];
}

/*
 * nonzero if [p, p+n) lies inside the slot that base points into.
 * Checking against the base the pointer was derived from, and not
 * against p itself, is what stops p = base + k from walking into the
 * neighbouring slot and looking valid there.
 */
static inline int lf_check(const void *base, const void *p, size_t n)
{
	uint64_t idx = lf_region(base);
	uint64_t size, start, d;

	if (idx >= LF_NREG)
		return 1;
	size = lf_size[idx];
	if (!size)
		return 1;
	start = (uint64_t)base - lf_offset((uint64_t)base, idx);
	d = (uint64_t)p - start;
	return d <= size && n <= size - d;
}

/* object whose size the compiler knows (stack, globals) */
static inline int lf_check_known(const void *base, const void *p, size_t n,
				 size_t size)
{
	uint64_t d = (uint64_t)p - (uint64_t)base;

	return d <= size && n <= size - d;
}

/* lo <= hi, both accesses of n bytes in bounds of base's slot */
static inline int lf_check_span(const void *base, const void *lo,
				const void *hi, size_t n)
{
	if ((uint64_t)hi < (uint64_t)lo)
		return 0;
	return lf_check(base, lo, n) && lf_check(base, hi, n);
}

#endif

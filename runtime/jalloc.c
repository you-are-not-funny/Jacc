// SPDX-License-Identifier: GPL-2.0-only WITH Jacc-runtime-exception
/*
 * Size-class allocator for Jacc.
 *
 * Every class owns a fixed 32 GiB slice of one big reservation, so the
 * class (and so the slot size) of any pointer falls out of its address.
 * Slots are never handed out twice: free() poisons, flips a state byte,
 * and gives physical pages back, but the address stays dead. That makes
 * double free and use-after-free deterministic at the cost of burning
 * address space. A sweeper that proves a slot unreferenced and recycles
 * it is the next piece of work (docs/SPEC.md, section 4).
 *
 * Allocations too big for any class get their own mapping and are not
 * bounds checked.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "jalloc.h"
#include "lowfat.h"

#define PAGE        4096UL
#define COMMIT_STEP (1UL << 20)
#define MAX_CLASS   (1UL << 24)
#define BIG_MAGIC   0x6a616363626967ULL

enum { SLOT_NEW, SLOT_LIVE, SLOT_FREED };

struct region {
	char *base;
	char *bump;
	char *committed;
	unsigned char *state;
	pthread_mutex_t lock;
};

struct big_hdr {
	uint64_t magic;
	uint64_t len;
};

uint64_t lf_first;
uint64_t lf_size[LF_NREG];
uint64_t lf_magic[LF_NREG];

static struct region reg[LF_NREG];
static int nclass;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int init_failed;

void jacc_trap(const char *what)
{
	static const char pre[] = "jacc: ";
	(void)!write(2, pre, sizeof pre - 1);
	(void)!write(2, what, strlen(what));
	(void)!write(2, "\n", 1);
	__builtin_trap();
}

static void build_classes(void)
{
	uint64_t s;

	for (s = 16; s <= 128; s += 16)
		lf_size[nclass++] = s;
	for (uint64_t oct = 128; oct < MAX_CLASS; oct *= 2)
		for (int i = 1; i <= 8; i++)
			lf_size[nclass++] = oct + i * (oct / 8);
}

static void do_init(void)
{
	size_t len = (size_t)(LF_NREG + 1) * LF_REGION;
	char *m = mmap(NULL, len, PROT_NONE,
		       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

	if (m == MAP_FAILED) {
		init_failed = 1;
		return;
	}
	m = (char *)(((uintptr_t)m + LF_REGION - 1) & ~(LF_REGION - 1));
	build_classes();

	for (int i = 0; i < nclass; i++) {
		size_t smap = (LF_REGION / lf_size[i] + PAGE - 1) & ~(PAGE - 1);
		struct region *r = &reg[i];

		lf_magic[i] = UINT64_MAX / lf_size[i] + 1;
		r->base = r->bump = r->committed = m + (size_t)i * LF_REGION;
		r->state = mmap(NULL, smap, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
				-1, 0);
		pthread_mutex_init(&r->lock, NULL);
		if (r->state == MAP_FAILED)
			init_failed = 1;
	}
	lf_first = (uint64_t)m >> LF_SHIFT;
}

static int class_for(size_t need)
{
	int lo = 0, hi = nclass - 1;

	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (lf_size[mid] >= need)
			hi = mid;
		else
			lo = mid + 1;
	}
	return lo;
}

static void *big_alloc(size_t n)
{
	size_t len = PAGE + ((n + PAGE - 1) & ~(PAGE - 1));
	char *m;

	if (len < n) {
		errno = ENOMEM;
		return NULL;
	}
	m = mmap(NULL, len, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m == MAP_FAILED)
		return NULL;
	((struct big_hdr *)m)->magic = BIG_MAGIC;
	((struct big_hdr *)m)->len = n;
	return m + PAGE;
}

void *jalloc_malloc(size_t n)
{
	size_t need = n + 1;	/* one past the end must stay inside the slot */
	struct region *r;
	char *p;
	int c;

	pthread_once(&once, do_init);
	if (init_failed) {
		errno = ENOMEM;
		return NULL;
	}
	if (need < n) {
		errno = ENOMEM;
		return NULL;
	}
	if (need > MAX_CLASS)
		return big_alloc(n);

	c = class_for(need);
	r = &reg[c];

	pthread_mutex_lock(&r->lock);
	p = r->bump;
	if (p + lf_size[c] > r->base + LF_REGION) {
		pthread_mutex_unlock(&r->lock);
		errno = ENOMEM;
		return NULL;
	}
	if (p + lf_size[c] > r->committed) {
		size_t grow = lf_size[c] > COMMIT_STEP ? lf_size[c] : COMMIT_STEP;

		grow = (grow + PAGE - 1) & ~(PAGE - 1);
		if (mprotect(r->committed, grow, PROT_READ | PROT_WRITE)) {
			pthread_mutex_unlock(&r->lock);
			errno = ENOMEM;
			return NULL;
		}
		r->committed += grow;
	}
	r->bump = p + lf_size[c];
	r->state[(size_t)(p - r->base) / lf_size[c]] = SLOT_LIVE;
	pthread_mutex_unlock(&r->lock);
	return p;	/* fresh pages are zero and slots are never reused */
}

void *jalloc_calloc(size_t n, size_t sz)
{
	size_t total;

	if (__builtin_mul_overflow(n, sz, &total)) {
		errno = ENOMEM;
		return NULL;
	}
	return jalloc_malloc(total);
}

size_t jalloc_usable(const void *p)
{
	uint64_t idx = lf_region(p);

	if (idx < LF_NREG && lf_size[idx])
		return lf_size[idx] - 1;
	return ((const struct big_hdr *)((const char *)p - PAGE))->len;
}

void jalloc_free(void *p)
{
	uint64_t idx;
	struct region *r;
	size_t size, slot;
	unsigned char *st;

	if (!p)
		return;
	idx = lf_region(p);

	if (idx >= LF_NREG || !lf_size[idx]) {
		struct big_hdr *h = (struct big_hdr *)((char *)p - PAGE);

		if (h->magic != BIG_MAGIC)
			jacc_trap("free of pointer jalloc does not own");
		munmap(h, PAGE + ((h->len + PAGE - 1) & ~(PAGE - 1)));
		return;
	}

	r = &reg[idx];
	size = lf_size[idx];
	if (lf_offset((uint64_t)p, idx))
		jacc_trap("free of interior pointer");
	slot = (size_t)((char *)p - r->base) / size;
	st = &r->state[slot];

	if (__atomic_exchange_n(st, SLOT_FREED, __ATOMIC_ACQ_REL) == SLOT_FREED)
		jacc_trap("double free");

	if (size < PAGE) {
		memset(p, 0xa5, size);
	} else {
		uintptr_t a = ((uintptr_t)p + PAGE - 1) & ~(PAGE - 1);
		uintptr_t b = ((uintptr_t)p + size) & ~(PAGE - 1);

		memset(p, 0xa5, a - (uintptr_t)p);
		if (b > a)
			madvise((void *)a, b - a, MADV_DONTNEED);
	}
}

void *jalloc_realloc(void *p, size_t n)
{
	void *q;
	size_t old;

	if (!p)
		return jalloc_malloc(n);
	if (!n) {
		jalloc_free(p);
		return NULL;
	}
	old = jalloc_usable(p);
	if (n <= old)
		return p;
	q = jalloc_malloc(n);
	if (!q)
		return NULL;
	memcpy(q, p, old);
	jalloc_free(p);
	return q;
}

void __jacc_check(const void *base, const void *p, size_t n)
{
	if (!lf_check(base, p, n))
		jacc_trap("out-of-bounds access");
}

void __jacc_check_span(const void *base, const void *lo, const void *hi,
		       size_t n)
{
	if (!lf_check_span(base, lo, hi, n))
		jacc_trap("out-of-bounds access (loop range)");
}

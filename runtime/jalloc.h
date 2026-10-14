/* SPDX-License-Identifier: GPL-2.0-only WITH Jacc-runtime-exception */
#ifndef JALLOC_H
#define JALLOC_H

#include <stddef.h>

void  *jalloc_malloc(size_t n);
void  *jalloc_calloc(size_t n, size_t sz);
void  *jalloc_realloc(void *p, size_t n);
void   jalloc_free(void *p);
size_t jalloc_usable(const void *p);

/* called by instrumented code when a check fails; never returns */
void   jacc_trap(const char *what) __attribute__((noreturn, cold));

/* what the compiler emits; see passes/ */
void   __jacc_check(const void *base, const void *p, size_t n);
void   __jacc_check_span(const void *base, const void *lo, const void *hi, size_t n);

#endif

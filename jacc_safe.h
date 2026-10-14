/* Force-included by jacc ahead of your code. Plain C like strcpy(buf, s)
 * keeps compiling, but gets routed to bounded versions when the compiler
 * can see how big buf is (arrays, malloc'd blocks; needs -O1 or higher).
 * If it can't see the size, the original libc call is left alone. */
#ifndef JACC_SAFE_H
#define JACC_SAFE_H

#include <limits.h>
#include <jacc.h>

#undef gets
#undef strcpy
#undef strcat
#undef sprintf
#undef vsprintf

#ifdef JACC_STRICT

#pragma GCC poison gets strcpy strcat sprintf vsprintf tmpnam mktemp

#else

#pragma GCC poison tmpnam mktemp

#define JACC_SZ(p)  __builtin_object_size((p), 1)
#define JACC_CAP(n) ((n) == (size_t)-1 ? (size_t)INT_MAX : (n))

static inline char *jacc_strcpy_(char *d, size_t sz, const char *s)
{
	if (sz == (size_t)-1)
		return strcpy(d, s);
	jacc_copy(d, sz, s);
	return d;
}

static inline char *jacc_strcat_(char *d, size_t sz, const char *s)
{
	if (sz == (size_t)-1)
		return strcat(d, s);
	jacc_append(d, sz, s);
	return d;
}

/* gets() is gone from C11 for good reason. This one reads a line with
 * fgets and strips the newline; it refuses to run if the size is unknown. */
static inline char *jacc_gets_(char *buf, size_t sz)
{
	if (sz == (size_t)-1 || sz == 0)
		jacc_die("gets: buffer size unknown (compile with -O1 or higher)");
	if (!fgets(buf, (int)(sz > INT_MAX ? INT_MAX : sz), stdin))
		return NULL;
	buf[strcspn(buf, "\n")] = '\0';
	return buf;
}

#define strcpy(d, s)       jacc_strcpy_((d), JACC_SZ(d), (s))
#define strcat(d, s)       jacc_strcat_((d), JACC_SZ(d), (s))
#define gets(b)            jacc_gets_((b), JACC_SZ(b))
#define sprintf(d, ...)    snprintf((d), JACC_CAP(JACC_SZ(d)), __VA_ARGS__)
#define vsprintf(d, f, ap) vsnprintf((d), JACC_CAP(JACC_SZ(d)), (f), (ap))

#endif
#endif

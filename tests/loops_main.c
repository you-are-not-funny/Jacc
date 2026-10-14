// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <stdlib.h>
#include "../runtime/jalloc.h"

long sum(long *a, long n);
void fill_down(int *a, long n);

int main(int argc, char **argv)
{
	long n = argc > 1 ? atol(argv[1]) : 10;
	long *a = jalloc_malloc(10 * sizeof *a);
	int *b = jalloc_malloc(10 * sizeof *b);

	for (int i = 0; i < 10; i++)
		a[i] = i;
	fill_down(b, n);
	printf("%ld\n", sum(a, n));
	return 0;
}

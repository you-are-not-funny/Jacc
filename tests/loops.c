// SPDX-License-Identifier: GPL-2.0-only
long sum(long *a, long n)
{
	long s = 0;
	for (long i = 0; i < n; i++)
		s += a[i];
	return s;
}

void fill_down(int *a, long n)
{
	for (long i = n - 1; i >= 0; i--)
		a[i] = (int)i;
}

/* index comes from memory: must keep its per-iteration check */
long gather(long *a, int *idx, long n)
{
	long s = 0;
	for (long i = 0; i < n; i++)
		s += a[idx[i]];
	return s;
}

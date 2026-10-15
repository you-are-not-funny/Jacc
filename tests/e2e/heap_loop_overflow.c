// expect: trap
#include <stdlib.h>
long sum(long *a, long n)
{
	long s = 0;

	for (long i = 0; i < n; i++)
		s += a[i];
	return s;
}

int main(int argc, char **argv)
{
	long *a = calloc(10, sizeof *a);

	return sum(a, 100 * argc) != 0;
}

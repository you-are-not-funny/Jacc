// expect: ok
// a stale read sees the poison byte, never another object's data
#include <stdlib.h>
int main(void)
{
	unsigned char *p = malloc(32);
	unsigned char *q;

	p[0] = 42;
	free(p);
	q = malloc(32);
	q[0] = 7;
	return (*(volatile unsigned char *)p == 0xa5) ? 0 : 1;
}

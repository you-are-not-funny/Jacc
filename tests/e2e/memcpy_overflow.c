// expect: trap
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
	char src[256] = {0};
	char *p = malloc(32);

	memcpy(p, src, 200 + argc);
	return p[0];
}

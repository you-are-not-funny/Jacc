// expect: trap
#include <stdlib.h>
int main(int argc, char **argv)
{
	char *p = malloc(100);
	p[argc + 499] = 1;
	return p[0];
}

// expect: trap
#include <stdlib.h>
#include <string.h>
int main(void)
{
	char *p = malloc(10);

	strcpy(p, "abcd");
	strcat(p, "this will not fit in the slot at all, no way");
	return p[0];
}

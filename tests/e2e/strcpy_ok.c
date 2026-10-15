// expect: ok
#include <string.h>
int main(void)
{
	const char *volatile src = "seven.";
	char dst[8];

	strcpy(dst, src);
	return strcmp(dst, "seven.") != 0;
}

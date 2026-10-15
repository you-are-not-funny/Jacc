// expect: trap
#include <string.h>
int main(void)
{
	const char *volatile src = "definitely longer than eight";
	char dst[8];

	strcpy(dst, src);
	return dst[0];
}

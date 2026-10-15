// expect: trap
// flags: -fjacc-int=trap
#include <limits.h>
int main(int argc, char **argv)
{
	volatile int x = INT_MAX;

	return x + argc;
}

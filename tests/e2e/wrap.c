// expect: ok
#include <limits.h>
int main(int argc, char **argv)
{
	volatile int x = INT_MAX;
	int y = x + argc;	/* argc >= 1 */

	return y == INT_MIN + argc - 1 ? 0 : 1;
}

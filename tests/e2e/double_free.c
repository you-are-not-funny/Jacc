// expect: trap
// p is stored somewhere visible, otherwise LLVM deletes the whole
// malloc/free pair and there is nothing left to trap on
#include <stdlib.h>
void *volatile keep;
int main(void)
{
	void *p = malloc(32);

	keep = p;
	free(p);
	free(p);
	return 0;
}

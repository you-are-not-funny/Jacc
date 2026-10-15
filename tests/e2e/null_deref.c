// expect: trap
int main(int argc, char **argv)
{
	volatile int *p = 0;

	return *p + argc;
}

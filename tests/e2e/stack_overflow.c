// expect: trap
int main(int argc, char **argv)
{
	char buf[16];

	for (int i = 0; i < 16; i++)
		buf[i] = 0;
	buf[argc + 20] = 1;
	return buf[0];
}

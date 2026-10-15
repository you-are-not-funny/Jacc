// expect: trap
int main(int argc, char **argv)
{
	int buf[16];
	int n = 15 + argc * 4;	/* 19 */

	for (int i = 0; i < n; i++)
		buf[i] = i;
	return buf[1];
}

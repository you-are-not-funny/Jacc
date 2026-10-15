// expect: ok
int main(int argc, char **argv)
{
	int buf[16], s = 0;

	for (int i = 0; i < 16; i++)
		buf[i] = i;
	for (int i = 0; i < 16; i++)
		s += buf[i];
	return s == 120 ? 0 : 1;
}

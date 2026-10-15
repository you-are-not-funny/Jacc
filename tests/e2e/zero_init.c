// expect: ok
// reading a local you never wrote gives zero, not whatever was on the stack
static int dirty(void)
{
	volatile char junk[256];
	for (int i = 0; i < 256; i++)
		junk[i] = 0x5a;
	return junk[3];
}

static int probe(void)
{
	int x;
	char b[256];
	long s = x;

	for (int i = 0; i < 256; i++)
		s += b[i];
	return s != 0;
}

int main(void)
{
	dirty();
	return probe();
}

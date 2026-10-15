// expect: trap
int main(int argc, char **argv)
{
	int n = argc + 3;
	int v[n];

	v[n + 5] = 1;
	return v[0];
}

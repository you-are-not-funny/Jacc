// expect: ok
int main(int argc, char **argv)
{
	int n = argc + 30, s = 0;
	int v[n];

	for (int i = 0; i < n; i++)
		v[i] = i;
	for (int i = 0; i < n; i++)
		s += v[i];
	return s == n * (n - 1) / 2 ? 0 : 1;
}

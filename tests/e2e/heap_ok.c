// expect: ok
// plain C that has to keep working: libc, stdio, qsort, realloc, strdup
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp(const void *a, const void *b)
{
	return *(const int *)a - *(const int *)b;
}

int main(void)
{
	int n = 1000, *v = malloc(sizeof *v);
	char *s, *t;
	FILE *f;

	for (int i = 0; i < n; i++) {
		v = realloc(v, (i + 1) * sizeof *v);
		v[i] = (i * 7919) % 1000;
	}
	qsort(v, n, sizeof *v, cmp);
	for (int i = 1; i < n; i++)
		if (v[i - 1] > v[i])
			return 1;

	s = strdup("hello, world");
	t = malloc(64);
	strcpy(t, s);
	strcat(t, "!");
	if (strcmp(t, "hello, world!"))
		return 2;

	f = fopen("/tmp/jacc_e2e.txt", "w");
	fprintf(f, "%d %s\n", n, t);
	fclose(f);
	f = fopen("/tmp/jacc_e2e.txt", "r");
	if (fscanf(f, "%d", &n) != 1 || n != 1000)
		return 3;
	fclose(f);

	free(s);
	free(t);
	free(v);
	return 0;
}

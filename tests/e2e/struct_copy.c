// expect: ok
struct big { long a[40]; };
static struct big g;
int main(void)
{
	struct big l = g;

	l.a[39] = 5;
	g = l;
	return g.a[39] == 5 ? 0 : 1;
}

// expect: trap
int table[8];
int main(int argc, char **argv)
{
	table[argc + 8] = 1;
	return table[0];
}

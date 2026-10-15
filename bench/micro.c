// SPDX-License-Identifier: GPL-2.0-only
// Four small kernels. Not SPEC, not representative; only here so the
// numbers in the README come from somewhere.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N (1 << 20)

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

/* streaming: every address is affine in i */
static double saxpy(double *x, double *y, long n, double a)
{
	for (long i = 0; i < n; i++)
		y[i] += a * x[i];
	return y[n / 2];
}

/* random reads: the index comes from memory, nothing to hoist */
static long gather(const long *tab, const int *idx, long n)
{
	long s = 0;

	for (long i = 0; i < n; i++)
		s += tab[idx[i]];
	return s;
}

/* nested loops on heap rows */
static void matmul(const double *a, const double *b, double *c, int n)
{
	for (int i = 0; i < n; i++)
		for (int j = 0; j < n; j++) {
			double s = 0;

			for (int k = 0; k < n; k++)
				s += a[i * n + k] * b[k * n + j];
			c[i * n + j] = s;
		}
}

struct node { struct node *next; long v; };

/* allocation heavy, pointer chasing */
static long list(int n)
{
	struct node *h = NULL, *p;
	long s = 0;

	for (int i = 0; i < n; i++) {
		p = malloc(sizeof *p);
		p->v = i;
		p->next = h;
		h = p;
	}
	for (p = h; p; p = p->next)
		s += p->v;
	while (h) {
		p = h->next;
		free(h);
		h = p;
	}
	return s;
}

static double best(double (*fn)(void *), void *arg)
{
	double b = 1e9;

	for (int r = 0; r < 5; r++) {
		double t = now();

		fn(arg);
		t = now() - t;
		if (t < b)
			b = t;
	}
	return b;
}

static volatile double sink;
static double *X, *Y, *A, *B, *C;
static long *T;
static int *IDX;

static double k_saxpy(void *u)  { for (int r = 0; r < 50; r++) sink = saxpy(X, Y, N, 1.0001); return 0; }
static double k_gather(void *u) { for (int r = 0; r < 20; r++) sink = gather(T, IDX, N); return 0; }
static double k_matmul(void *u) { matmul(A, B, C, 400); sink = C[5]; return 0; }
static double k_list(void *u)   { sink = list(1500000); return 0; }

int main(void)
{
	X = malloc(N * sizeof *X);
	Y = malloc(N * sizeof *Y);
	A = malloc(400 * 400 * sizeof *A);
	B = malloc(400 * 400 * sizeof *B);
	C = malloc(400 * 400 * sizeof *C);
	T = malloc(N * sizeof *T);
	IDX = malloc(N * sizeof *IDX);

	for (long i = 0; i < N; i++) {
		X[i] = i; Y[i] = 1; T[i] = i;
		IDX[i] = (int)((i * 2654435761u) % N);
	}
	for (int i = 0; i < 160000; i++) { A[i] = i % 7; B[i] = i % 5; }

	printf("saxpy  %.3f\n", best(k_saxpy, 0));
	printf("gather %.3f\n", best(k_gather, 0));
	printf("matmul %.3f\n", best(k_matmul, 0));
	printf("list   %.3f\n", best(k_list, 0));
	return 0;
}

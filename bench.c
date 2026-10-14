#include <stdio.h>
#include <stdlib.h>
#define M 384
static double A[M][M], B[M][M], C[M][M];
static unsigned v[1<<20];
static void isort(unsigned *x, int n){ for(int i=1;i<n;i++){unsigned k=x[i];int j=i-1;while(j>=0&&x[j]>k){x[j+1]=x[j];j--;}x[j+1]=k;} }
static unsigned rng=12345; static unsigned nx(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void){
  for(int i=0;i<M;i++)for(int j=0;j<M;j++){A[i][j]=i+j;B[i][j]=i-j;}
  for(int i=0;i<M;i++)for(int k=0;k<M;k++){double a=A[i][k];for(int j=0;j<M;j++)C[i][j]+=a*B[k][j];}
  for(int i=0;i<(1<<20);i++)v[i]=nx();
  int n=12000; isort(v,n);
  unsigned h=0; for(int i=0;i<n;i++)h=h*31+v[i];
  printf("%f %u\n",C[M-1][M-1],h); return 0; }

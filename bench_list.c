#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct node { struct node *next; unsigned key; char name[24]; };
static unsigned h(const char *s) { unsigned x = 5381; while (*s) x = x * 33 + (unsigned char)*s++; return x; }
static int cmp(const void *a, const void *b) { unsigned x = *(const unsigned *)a, y = *(const unsigned *)b; return (x > y) - (x < y); }
int main(void) {
  struct node *head = NULL; unsigned *keys = malloc(300000 * sizeof *keys);
  for (int rep = 0; rep < 12; rep++) {
    for (int i = 0; i < 300000; i++) {
      struct node *n = malloc(sizeof *n); snprintf(n->name, sizeof n->name, "item-%d-%d", rep, i);
      n->key = h(n->name); n->next = head; head = n; keys[i] = n->key; }
    qsort(keys, 300000, sizeof *keys, cmp);
    while (head) { struct node *t = head->next; free(head); head = t; }
  }
  printf("%u\n", keys[1234]); free(keys); return 0; }

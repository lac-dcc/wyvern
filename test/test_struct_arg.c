#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int GLOBAL;

struct myStruct {
  int (*fptr)(int);
  int x;
  int y;
};

int foo(struct myStruct *str) {
  return str->x + str->y;
}

int callee (int n) {
  if (GLOBAL == 2) {
    return 0;
  }
  return n;
}

int caller(int n) {
  struct myStruct test;
  test.x = n * 2;
  test.y = n * 3;
  return callee(foo(&test));
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Syntax: %s N\n", argv[0]);
  } else {
    const int N = atoi(argv[1]);
    GLOBAL = N;
    printf("%d\n", caller(GLOBAL * GLOBAL));
  }
}

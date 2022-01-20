#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int target(char* s0, char* s1, int is_num0, int is_num1, int N) {
  int sum = 0;
  if (is_num0) {
    if (is_num1) {
      for (int i = 0; i < N; i++) {
        sum += s0[i] + s1[i];
      }
    }
  }
  return sum;
}

int caller(char* s0, char* s1, int N) {
  int is_num0 = 1;
  int is_num1 = 1;
  for (int i = 0; i < N; i++) {
    if (s0[i] < '0' || s0[i] > '9') {
      is_num0 = 0;
      break;
    }
  }
  for (int i = 0; i < N; i++) {
    if (s1[i] < '0' || s1[i] > '9') {
      is_num1 = 0;
      break;
    }
  }
  return target(s0, s1, is_num0, is_num1, N);
}

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "Syntaxe: %s N\n", argv[0]);
  } else {
    const int N = atoi(argv[1]);
    char* s0 = malloc(N);
    char* s1 = malloc(N);
    memset(s0, 'a', N);
    memset(s1, '2', N);
    printf("%d\n", caller(s0, s1, N));
  }
}

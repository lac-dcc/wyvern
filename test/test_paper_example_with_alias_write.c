#include <stdio.h>
#include <stdlib.h>

void callee(int key, int value, int N) {
  if (key != 0 && value < N) {
    printf("User has access\n");
  }
}

void caller(char *s0, int *keys, int N) {
  int key = atoi(s0);
  int value = -1;

  // write to load used in slice through aliasing pointer
  int *keys2 = keys;
  keys2[0] = 10;

  for (int i = 0; i < N; i++) {
    if (keys[i] == key) {
      value = i;
    }
  }
  callee(key, value, N);
}

int main() {
  int keys[1000000];
  for (int i = 0; i < 1000000; i++) {
    keys[i] = rand();
  }

  int num_strs;
  char str[100];
  fscanf(stdin, "%d\n", &num_strs);
  for (int i = 0; i < num_strs; i++) {
    fscanf(stdin, "%s\n", str);
    caller(str, keys, 1000000);
  }
}

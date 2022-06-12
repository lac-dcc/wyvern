#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline))
int callee(int key, int value, int N) {
  if (key != 0 && value < N) {
    printf("User has access\n");
    return 1;
  }
  return 0;
}

__attribute__((noinline))
int caller(char *s0, int *keys, int N) {
  int key = atoi(s0);
  int value = -1;
  for (int i = 0; i < N; i++) {
    if (keys[i] == key) {
      value = i;
    }
  }
  if (callee(key, value, N)) {
    return value;
  }
  return 0; 
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

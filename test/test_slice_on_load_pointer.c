// This simple test contains two functions which have paths where at least
// one of their arguments is not used, and should thus be candidates for
// lazyfication. It exits through the exit() system call rather than returning
// from main.

#include <stdio.h>
#include <stdlib.h>

int maybe_use_arg(int always_used, int *maybe_used);

#define SIZE 10000

int optimizable(int x) {
	int *maybe;
	posix_memalign((void**) &maybe, sizeof(void*), SIZE);
	return maybe_use_arg(x, maybe);
}

int maybe_use_arg(int always_used, int *maybe_used) {
	if (always_used > 0) {
		int i, sum = 0;
		for (i = 1; i < SIZE; ++i) {
			maybe_used[i] = i * i;
			sum += maybe_used[i-1];
		}
		fprintf(stdout, "sum = %d\n", sum);
		return 1;
	}

	else {
		return 0;
	}
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <value1>\n", argv[0]);
		return 0;
	}
	return optimizable(atoi(argv[1]));
}

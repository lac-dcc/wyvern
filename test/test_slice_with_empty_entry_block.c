// This simple test contains two functions which have paths where at least
// one of their arguments is not used, and should thus be candidates for
// lazyfication. It exits through the exit() system call rather than returning
// from main.

#include <stdio.h>
#include <stdlib.h>

int maybe_use_arg(int w, int z);

int optimizable(int it) {
	int maybe = 0;
	for (int i = 0; i < it; i++) {
		maybe += it * it;
	}
	return maybe_use_arg(10, maybe);
}

int maybe_use_arg(int always_used, int maybe_used) {
	if (always_used > 0) {
		fprintf(stdout, "maybe_used = %d\n", maybe_used);
		return 1;
	}

	else {
		return 0;
	}
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <value1> \n", argv[0]);
		return 0;
	}
	return optimizable(atoi(argv[1]));
}

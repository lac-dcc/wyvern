// This simple test contains two functions which have paths where at least
// one of their arguments is not used, and should thus be candidates for
// lazyfication. It exits through the exit() system call rather than returning
// from main.

#include <stdio.h>
#include <stdlib.h>

int maybe_use_arg(int data);

int ncol = 1000;

int optimizable(int test) {
	if (test == 0) { return 0; }

	int blah = 2;
	if (test > 5) {
		blah = 3;
	}
	return maybe_use_arg(blah);
}

int maybe_use_arg(int data) {
	int new_data;
	int counter = ncol;
	while(counter--) {
		new_data--;
	}
	if (new_data == 0) {
		return new_data + data;
	}
	return new_data;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <value1>\n", argv[0]);
		return 0;
	}
	return optimizable(atoi(argv[1]));
}

// This simple test contains two functions which have paths where at least
// one of their arguments is not used, and should thus be candidates for
// lazyfication. It exits through the exit() system call rather than returning
// from main.

#include <stdio.h>
#include <stdlib.h>

int bar(int x, int y) {
	long long bits = 0;

	if (x >= 10) {
		fprintf(stdout, "y = %d\n", y);
	}

	return 0;
}

int foo(int x, int y, int z, int i) {
	long long bits = 0;

	if (x > 0) {
		fprintf(stdout, "num = %d\n", y * 2 + z - i);
		return 1;
	}

	else {
		return 0;
	}
}

void dummyExit() {
	exit(0);
}

int main() {
	bar(10, 5);
	bar(1, 5);
	foo(10, 11, 12, 13);
	foo(-1, 11, 12, 13);
	dummyExit();
	return 0;
}

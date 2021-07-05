// This simple test contains two functions which have paths where at least
// one of their arguments is not used, and should thus be candidates for
// lazyfication.

#include <stdio.h>

int bar(int x, int y) {
	if (x >= 10) {
		fprintf(stdout, "y = %d\n", y);
	}

	return 0;
}

int foo(int x, int y, int z, int i) {
	if (x > 0) {
		fprintf(stdout, "num = %d\n", y * 2 + z - i);
		return 1;
	}

	else {
		return 0;
	}
}

int main() {
	bar(10, 5);
	bar(1, 5);
	foo(10, 11, 12, 13);
	foo(-1, 11, 12, 13);
	return 0;
}

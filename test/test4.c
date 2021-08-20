// This simple test contains two functions which have paths where at least
// one of their arguments is not used, and should thus be candidates for
// lazyfication.

#include <stdio.h>

int getInput() {
	int input = 5;
	return input;
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

int bar(int x, int y) {
	if (x >= 10) {
		fprintf(stdout, "y = %d\n", y);
	}

	int a = 10 + getInput();
	int b;

	if (a > 10) {
		b = getInput();
	}
	else {
		b = getInput() + 15;
	}
	int z = foo(5, a * b * 3, 10, 10);

	return z;
}

int main() {
	int input = getInput();
	bar(input, 5);
	bar(input, 5);
	foo(input, 11, 12, 13);
	foo(input, 11, 12, 13);
	return 0;
}

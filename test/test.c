#include <stdio.h>

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

int main() {
	bar(10, 5);
	bar(1, 5);
	foo(10, 11, 12, 13);
	foo(-1, 11, 12, 13);
	return 0;
}

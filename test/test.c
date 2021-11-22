// This simple test contains two functions which have paths where at least
// one of their arguments is not used, and should thus be candidates for
// lazyfication.

#include <stdio.h>

int optimizable(int x, int y) {
	if (x >= 10) {
		fprintf(stdout, "y = %d\n", y);
	}

	return 0;
}

int maybe_use_z(int x, int y, int z, int i) {
	if (x > 0) {
		fprintf(stdout, "num = %d\n", y * 2 + z - i);
		return 1;
	}

	else {
		return 0;
	}
}

int main() {
	optimizable(10, 5);
	optimizable(1, 5);
	maybe_use_z(10, 11, 12, 13);
	maybe_use_z(-1, 11, 12, 13);
	return 0;
}

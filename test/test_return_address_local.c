// This simple test contains two functions which have paths where at least
// one of their arguments is not used, and should thus be candidates for
// lazyfication.

#include <stdio.h>

struct myStruct {
	int x;
	int y;
};

int maybe_use_z(int x, int y, struct myStruct *z, int i);

int optimizable(int x, int y) {
	struct myStruct test;
	test.x = 5;

	return maybe_use_z(x, y, &test, x + y);
}

int maybe_use_z(int x, int y, struct myStruct *z, int i) {
	if (x+y+i > 0) {
		fprintf(stdout, "num = %d\n", y * 2 + z->x - i);
		return 1;
	}

	else {
		return 0;
	}
}

int main() {
	optimizable(10, 5);
	return 0;
}

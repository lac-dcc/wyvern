// This simple test contains two functions which have paths where at least
// one of their arguments is not used, and should thus be candidates for
// lazyfication.

#include <stdio.h>
#include <stdlib.h>

int maybe_use_arg(int w, int z);

int dummy(void) {
	return 10;
}

int optimizable(int x, int y) {
    int i, l;

    for (i = 0; i < 10; i++) {
        l += maybe_use_arg(x, i);
	}

	return l + y;
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
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <value1> <value2>\n", argv[0]);
		return;
	}

	return optimizable(atoi(argv[1]), atoi(argv[2]));
}

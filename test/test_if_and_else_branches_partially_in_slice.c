// This simple test contains two functions which have paths where at least
// one of their arguments is not used, and should thus be candidates for
// lazyfication.

#include <stdio.h>
#include <stdlib.h>

int maybe_use_arg(int w, int z);

int optimizable(int x, int y) {
	int maybe = x + y;
	int pointless = x * y;

	if (x > 10) {
		if (y < 10) {
			pointless = 10 * y;
		}
		else {
			pointless = 10 * x;
		}
		maybe = x * x + y;
	}

	else {
		maybe = x + x * y;
		if (y > 10) {
			pointless = 10 + y;
		}
		else {
			pointless = 10 + x;
		}
	}

	fprintf(stdout, "pointless = %d\n", pointless);
	return maybe_use_arg(x, maybe);
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
		return 0;
	}

	return optimizable(atoi(argv[1]), atoi(argv[2]));
}

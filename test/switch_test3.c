// This simple test contains two functions which have paths where at least
// one of their arguments is not used, and should thus be candidates for
// lazyfication. It exits through the exit() system call rather than returning
// from main.

#include <stdio.h>
#include <stdlib.h>

int maybe_use_arg(int w, int z);

int a;

int dummy(void) {
	return a;
}

int optimizable() {
	int x = dummy();
	int y = dummy();
	int maybe = 10 * x;
	int pointless = 10 * y;

	for(int i = 0; i < x; i++) {
		maybe+=y;
	}

	if (x + y > 10) {
		if (y % x == 2) {
			switch (x) {
				case 0: {
					maybe = x * y;
					break;
				}
				case 1: {
					maybe = y*y*5;
					break;
				}
				case 2: {
					pointless = x + y + y * y;
					break;
				}
				default: {
					pointless = x + x * x + y;
					break;
				}
			}
		}
	}

	else {
		maybe = y * x / 2;
		if (maybe % 3 == 0) {
			maybe = y * y * x;
		}
	}

	fprintf(stdout, "teste %d\n", pointless);
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
	}

	a = argc;
	return optimizable();
}

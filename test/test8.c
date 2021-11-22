// This simple test contains two functions which have paths where at least
// one of their arguments is not used, and should thus be candidates for
// lazyfication.

#include <stdio.h>
#include <stdlib.h>

struct test {
	int x;
};

int maybe_use_arg(int w, int z);

int dummy(void) {
	return 10;
}

int optimizable(struct test t) {
	int pointless = 0;
	int x = dummy();
	int y = dummy();

	if (x + y) {
		if (x - y) {
			if (x * y) {
				int maybe = t.x;
				maybe_use_arg(x, maybe / 2.0);
			} else {
				pointless = x * y;
			}
		} else {
			pointless = x - y;
		}
	} else {
		pointless = y - x;
	}
	
	return pointless;
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

	struct test t;
	t.x = dummy();
	return optimizable(t);
}

#include <stdio.h>
#include <math.h>

int foo (int i, int x) {
	if (i == 999999) {
		fprintf(stdout, "%d\n", x);
	}
	else if (i != 999999) {
		fprintf(stdout, "%d\n", 0);
	}
	return i;
}

int main(int argc, char** argv) {
	int i, j;

	for (i = 0; i < 1000000; i++) {
        double x = 0.0;
        for (j = 0; j < 10 * argc; j++) {
          x += pow(2.0, sin(j) * cos(j));
        }
		foo(i, x);
	}

	return 0;
}

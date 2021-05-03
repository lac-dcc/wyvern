// This test contains a function (foo) which has an argument that, although
// may seem to be used in the LLVM IR, is actually not (@y). This is because
// LLVM treats Phi nodes as uses, whereas they might not necessarily be so.
// In this case, @y is only used in the "else" branch of the conditional.
// However, due to LLVM's promotion to registers, @y is not actually used
// at that BasicBlock, and instead the use is "moved" down to the join point
// for @z.

#include <stdio.h>

int foo(int x, int y) {
	int z;
	
	if (x >= 10) {
		z = 10;
	}

	else {
		z = y;
	}

	if (z == 10) {
		fprintf(stdout, "z = %d\n", z); 
		return 1;
	}

	return 0;
}

int main() {
	foo(5, 10);
	foo(10, 15);
	return 0;
}

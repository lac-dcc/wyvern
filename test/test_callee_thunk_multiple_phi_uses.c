// This test contains a lazification instance where, in the callee function, the
// thunk argument contains multiple uses within a phi function, with different
// incoming blocks. For instance:
// %wrong = phi i32 [%thk, %block1], [%thk, %block2], [%some_other_val, %block3]
// In this case, loading the thunk argument in either %block1 or %block2 is
// incorrect, because these blocks may not necessarily dominate the phi.
// Instead, we need to either load the thunk value in a common dominator of both
// blocks, or add one load for each block.

#include <stdio.h>
#include <stdlib.h>

int maybe_use_arg(int w, int z);

int optimizable(int x, int y) {
  int maybe = 10 * x;
  int pointless = 10 * y;

  for (int i = 0; i < x; i++) {
    pointless += x;
    maybe += y;
  }
  return maybe_use_arg(x, maybe);
}

int maybe_use_arg(int always_used, int maybe_used) {
  if (always_used > 10) {
    return 0;
  }

  else {
    int ret = 10;
    switch (always_used) {
    case 0: {
      ret = maybe_used;
      break;
    }
    case 1: {
      ret = maybe_used;
      break;
    }
    case 2: {
      ret = always_used;
      break;
    }
    default: {
      ret = 10;
      break;
    }
    }
    return ret;
  }
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <value1> <value2>\n", argv[0]);
    return 0;
  }

  return optimizable(atoi(argv[1]), atoi(argv[2]));
}

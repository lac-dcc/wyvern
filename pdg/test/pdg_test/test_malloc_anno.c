#include<stdlib.h>

__attribute__((noinline))
void *pz_malloc(size_t size) { return malloc(size);}

__attribute__((noinline))
void pz_free(void* p) { free(p); }

int main(int argc, char **argv) {
   __attribute__((annotate("persist")))
   int* p_r2 = (int*)pz_malloc(sizeof(int));
   *p_r2 = 7;
    pz_free(p_r2);
    return 0;
}

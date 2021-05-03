#include <stdlib.h>
#include <stdio.h>

long long **_wyinstr_log;

void _wyinstr_init(int num_functions) {
	_wyinstr_log = (long long **) malloc(num_functions * sizeof(long long *));
	long long i;
	for (i = 0; i < num_functions; i++) {
		_wyinstr_log[i] = (long long *) malloc(2 * sizeof(long long));
	}
}

void _wyinstr_dump(int num_functions) {
	int i;
	FILE *outfile = fopen("wyinstr_output.csv", "w");
	fprintf(outfile, "fun_id,runs,runs_with_all_used\n");
	for (i = 0; i < num_functions; i++) {
		fprintf(outfile, "%d,%lli,%lli\n", i, _wyinstr_log[i][0], _wyinstr_log[i][1]);
	}
	fclose(outfile);

	for (i = 0; i < num_functions; i++) {
		free(_wyinstr_log[i]);
	}
	free(_wyinstr_log);
}

void _wyinstr_mark(long long *bits, int arg) {
	*bits =  *bits | (1<<arg);
}

long long _wyinstr_initbits() {
	return 0LL;
}

void _wyinstr_log_func(long long *bits, int size, long long func_id) {
	int i;
	#ifdef DEBUG
	fprintf(stdout, "[ ");
	for (i = 0; i < size-1; i++) {
		fprintf(stdout, "%d, ", (*bits&(1<<i)) != 0);
	}
	fprintf(stdout, "%d ]\n", (*bits&(1<<i)) != 0);
	fprintf(stdout, "1<<size-1: %d, bits: %d\n", (1<<size)-1, *bits);
	#endif
	
	_wyinstr_log[func_id][0] += 1;
	_wyinstr_log[func_id][1] += (1<<size)-1 == *bits;	
}

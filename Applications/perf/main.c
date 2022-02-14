#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NS_PER_MS	(1000000000)
#define N_INSTR_PER_LOOP	(8)

int main(int argc, char* argv[])
{
	struct timespec start_time, stop_time;
	unsigned long i, n_loops, n_instr, elapsed_ns;

	printf("PERFMEAS - Userspace performance benchmark\n");
	printf("  Usage : perfmeas [instruction count (default=7000000)]\n");

	if (argc == 2){
		n_instr = atol(argv[1]);
	} else {
		n_instr = 7000000;
	}

	if (n_instr < 1000) {
		printf("Error : instruction count is too low (< 1000)\n");
		goto error;
	} 

	n_loops = n_instr / N_INSTR_PER_LOOP;

	clock_gettime(CLOCK_MONOTONIC, &start_time);
	
	// Loop performing the requested number of instructions
	for (i = 0; i < n_loops; i++);

	clock_gettime(CLOCK_MONOTONIC, &stop_time);

	elapsed_ns = (stop_time.tv_sec - start_time.tv_sec) * NS_PER_MS + (stop_time.tv_nsec - start_time.tv_nsec);

	printf("Result : %lu loops (%lu instructions) achieved in %lu [us]\n", n_loops, n_instr, elapsed_ns / 1000);
	printf("   Performance is %lu MIPS\n", ((unsigned long)n_instr)  / (elapsed_ns / 1000ul));

	return 0;
error:
	return -1;
}

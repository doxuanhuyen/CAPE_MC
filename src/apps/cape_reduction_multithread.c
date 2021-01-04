#include <math.h>
#include <stdio.h>
#include "mpi.h"
#include <sys/time.h>
#include "../../include/cape.h"
void * __get_pc () { return __builtin_return_address(0); }
#define N 20000

int   i,THREADS=2;;
double a[N], sum; 
int main (int argc, char *argv[]) 
{	
	cape_declare_variable(&i,CAPE_INT, 1, 0);
	cape_declare_variable(&a, CAPE_DOUBLE, N, 0);
	cape_declare_variable(&sum, CAPE_DOUBLE, 1, 0);

	/* Some initializations */	
	for (i=0; i < N; i++)
		a[i] = 1.0;
	sum = 0.0;
	
	cape_init();
	cape_begin(PARALLEL_FOR, 0, N);
	cape_set_private(&i);
	cape_set_reduction(&sum, CAPE_SUM);
	ckpt_start();
	double sumi=0; 	
	#pragma omp parallel for schedule(static)  private(i) reduction(+:sum) num_threads(THREADS)		
		for (i= __left__; i < __right__; i++)
		{
			sum = sum + a[i];
			//if ((cape_get_node_num()==0) && (i==__left__+1 || i==__left__+10 || i==__right__-2 || i==__right__-1 )) //printf("Node %d: i: %d   Sum = %lf\n", cape_get_node_num(), i,sumi);			
		}
		printf("...\n");
	//sum=sum;
	printf("Node %d:    Sum = %lf\n", cape_get_node_num(), sum);
		__pc__ = (unsigned long) __get_pc();		
	cape_end(PARALLEL_FOR, TRUE);
	
	printf("Node %d:    Sum = %lf\n", cape_get_node_num(), sum);
	
	cape_finalize();
}

#include <math.h>
#include <stdio.h>
#include "mpi.h"
#include <sys/time.h>
#include "../../include/cape.h"

#define NUM_STEPS 1600000000


void * __get_pc () { return __builtin_return_address(0); }

unsigned long get_ms_of_day(){
	struct timeval tv;
	char st[40];
	gettimeofday(&tv, NULL); 
	sprintf(st, "%ld", tv.tv_usec);
	st[4] = '\0';
	return tv.tv_sec * 1000 + (strtol(st, NULL, 10) + 5) / 10;
}

double x , aux;
double sum, pi;
double step;
long long i;
int NUM_THREADS=4;

void main(){
	unsigned int t0=0,t1=0,t2=0,t3=0,t4=0;
	double tm0 = 0.0, tm4 = 0.0 ,tm3=0.0,tm2=0.0,tm1=0.0;  
	t0= get_ms_of_day() ;
	
	cape_declare_variable(&x, CAPE_DOUBLE, 1, 0);
	cape_declare_variable(&aux, CAPE_DOUBLE, 1, 0);
	cape_declare_variable(&sum, CAPE_DOUBLE, 1, 0);
	cape_declare_variable(&pi, CAPE_DOUBLE, 1, 0);
	cape_declare_variable(&step, CAPE_DOUBLE, 1, 0);
	cape_declare_variable(&i, CAPE_LONG, 1, 0);	
	cape_init();
	tm0 = MPI_Wtime(); 
	x = 0;
	sum = 0.0;
	step = 1.0 / (double) NUM_STEPS;
	
	//#pragma omp parallel private(i,x,aux) shared(sum)
	cape_begin(PARALLEL, 0, 0);
	cape_set_private(&i);
	cape_set_private(&x);
	cape_set_private(&aux);
	cape_set_shared(&sum);
	ckpt_start();
	{			
		cape_begin(FOR, 0, NUM_STEPS);
		cape_set_reduction(&sum, CAPE_SUM);
		ckpt_start();		
		t1= get_ms_of_day() ;
		tm1= MPI_Wtime(); 
		#pragma omp parallel for reduction(+:sum) private(i) num_threads(NUM_THREADS)
		for(i=__left__; i< __right__; i++){
			x = (i+ 0.5) * step;
			aux = 4.0/(1.0 + x*x);
			sum += aux;
		}
		t2= get_ms_of_day() ;
		tm2= MPI_Wtime(); 
		__pc__ = (unsigned long) __get_pc();
		cape_end(FOR, TRUE);
	}
	t3= get_ms_of_day() ;
	tm3= MPI_Wtime(); 
	__pc__ = (unsigned long) __get_pc();
	cape_end(PARALLEL, FALSE);	
	pi = step * sum;	
	
	t4= get_ms_of_day() ;
	tm4= MPI_Wtime(); 
	//printf("Node %d: sum =%lf,  pi = %lf\n", cape_get_node_num(), sum, pi );
	//if (cape_get_node_num() == 0)
	{
		//printf("%d\t%lf\t%ld\n", NUM_STEPS, pi, t4-t0);
		printf("%c	%d	%d	%f	%f	%f	%f	%f	%lf  %llu	%llu	Kind;node;numthreads;TotalTime;init;calculator;Reduce;Bcast;PI;Left;Right\n",'s',cape_get_node_num(),NUM_THREADS,tm4-tm0,tm1-tm0,tm2-tm1,tm3-tm2,tm4-tm3,pi,__left__,__right__);
		//printf("%c	%d	%d	%f	%f	%f	%f	%f	%lf  %llu	%llu	np;TotalTime;init;calculator;Reduce;Bcast;PI;\n",'s',cape_get_node_num(),NUM_THREADS,t4-t0,t1-t0,t2-t1,t3-t2,t4-t3,pi,__left__,__right__);
	}
	cape_finalize();
}

  

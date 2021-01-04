#include <math.h>
#include <stdio.h>
#include "mpi.h"
#include <sys/time.h>
#include <omp.h>

#include "../../include/cape.h"
void * __get_pc () { return __builtin_return_address(0); }

unsigned long get_ms_of_day(){
	struct timeval tv;
	char st[40];
	gettimeofday(&tv, NULL); 
	sprintf(st, "%ld", tv.tv_usec);
	st[4] = '\0';
	return tv.tv_sec * 1000 + (strtol(st, NULL, 10) + 5) / 10;
}

#define N 1600
int A[N][N], B[N][N], C[N][N];

int i, j,k, THREADS=4;
long sum;

    unsigned long t0= 0, t1 =0, t2 =0, t3 =0, t4 =0;
    double tm0 = 0.0, tm4 = 0.0 ,tm3=0.0,tm2=0.0,tm1=0.0; 

void main(){	
double tm0 = omp_get_wtime();
t0 = get_ms_of_day(); 	
	cape_declare_variable(&A, CAPE_INT, N*N, 0);
	cape_declare_variable(&B, CAPE_INT, N*N, 0);
	cape_declare_variable(&C, CAPE_INT, N*N, 0);
	cape_declare_variable(&i, CAPE_INT, 1, 0);
	cape_declare_variable(&j, CAPE_INT, 1, 0);
	cape_declare_variable(&k, CAPE_INT, 1, 0);
	cape_declare_variable(&sum, CAPE_LONG, 1, 0);
	cape_init();
	tm0 = omp_get_wtime();
	//load data		
	for(i=0;i<N;i++)
		for(j=0;j<N;j++){
                  A[i][j]= 1;   
                  B[i][j]= 1;                             
    }
	tm1 = omp_get_wtime();	
	t1 = get_ms_of_day()    ;
	cape_begin(PARALLEL_FOR, 0, N);	
	ckpt_start();
	tm2 = omp_get_wtime();	
	t2 = get_ms_of_day();
	//THREADS = omp_get_max_threads(); 
	//printf("Node %d: NumThread =%d\n", cape_get_node_num(), THREADS);

	//#pragma omp parallel for num_threads(THREADS) 
	//#pragma omp parallel for schedule(dynamic)  private(j,k) shared(C) num_threads(THREADS)	
	//#pragma omp parallel for schedule(static)  private(j,k) shared(C) num_threads(THREADS)
	#pragma omp parallel for schedule(static)  private(i,j,k,sum) shared(C) num_threads(THREADS)			
	for(i = __left__; i < __right__; i ++)
	{
		for(j = 0; j < N; j++ )
			{
				sum = 0 ;
				for ( k = 0; k < N; k++)
				{
					sum = sum + A[i][k] * B[k][j];
				}

				C[i][j] = sum;
			}			
	} 
	tm3 = omp_get_wtime();	
	t3 = get_ms_of_day(); 	
	__pc__ = (unsigned long) __get_pc();

	cape_end(PARALLEL_FOR, FALSE); //TRUE: if exist reduction clause, FALSE: if it does not
    cape_finalize();
	//printf("Node %d: C[10][10] =%d\n", cape_get_node_num(), C[10][10]);
	//printf("Node %d: C[N-1][N-1] =%d\n", cape_get_node_num(), C[N-1][N-1]);
	//printf("Node %d: C[N/2][N/2] =%d\n", cape_get_node_num(), C[N/2][N/2]);   
	//printf("Node %d: C[3][10] =%d\n", cape_get_node_num(), C[3][10]); 
	//printf("Node %d: C[9][10] =%d\n", cape_get_node_num(), C[9][10]); 
    tm4 = omp_get_wtime();	
	t4 = get_ms_of_day(); 	
	//double time = omp_get_wtime() - start_time;
	printf("%c\t%d\t%d\t%d\t%g\t%g\t%g\t%g\t%g\t Find;thread;Node:N:TotalTime;initcape;initdata;cal;final\n", 's',THREADS,cape_get_node_num(),N, tm4-tm0,tm1-tm0,tm2-tm1,tm3-tm2,tm4-tm3);
	printf("%c\t%d\t%d\t%d\t%ld\t%ld\t%ld\t%ld\t%ld\t Find;thread;Node:N:TotalTime;initcape;initdata;cal;final\n", 'm',THREADS,cape_get_node_num(),N, t4-t0, t1-t0, t2-t1, t3-t2, t4-t3);
}

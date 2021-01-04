#!/usr/bin/env bash
prog5=cape_parallel_for_multthread4c_1600
prog6=cape_pi_4c_1600000000
prog7=cape_prime
prog8=cape_critical
prog9=cape_reduction
prog10=cape_for
prog11=cape_for_nowait
prog12=cape_mamult2d
prog11=cape_for_nowait
prog12=cape_mamult2d
prog13=cape_parallel_for
prog14=cape_reduction_multithread
prog15=cape_critical2
max5=2
max6=10

source ip_config.sh

for i in `seq 1 $max1`;
do
	date '+%X'	
	echo Program $prog5 is executing ... $i of $max5
	#mpirun --host ${master} ~/${folder}/bin/${progx}
	#mpirun --host ${master},${node1},${node2},${node3},${node4},${node5},${node6},${node7},${node8},${node9},${node10},${node11},${node12},${node13},${node14},${node15} ~/${folder}/bin/${progx} 
	mpirun --host ${master},${node1},${node2},${node3},${node4},${node5},${node6},${node7},${node8},${node9},${node10},${node11},${node12},${node13},${node14},${node15} ~/${folder}/bin/${prog5} >> cape704_time_16nodes2020_${prog5}.txt
	
done 

echo The execution of $prog5 is finished.



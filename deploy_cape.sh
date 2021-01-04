#!/usr/bin/env bash
source ip_config.sh

#ssh ${uid}@${master} << EOF
#  if [ ! -d ${folder} ]; then
#		mkdir ~/${folder}
#  fi
#EOF
#scp -r bin include ${uid}@${master}:~/${folder}/
#scp ./*.sh makefile README.TXT ${uid}@${master}:~/${folder}/

for i in $(seq 1 $nslave_nodes)
do

eval "node=\$node$i"
ssh ${uid}@${node} << EOF
	if [ ! -d ${folder} ]; then
			mkdir ~/${folder}
	fi
	
EOF
	
scp -r bin include ${uid}@${node}:~/${folder}/
scp ./*.sh ${uid}@${node}:~/${folder}/
	
done

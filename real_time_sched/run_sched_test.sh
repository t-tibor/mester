#!/bin/bash
if [[ ! $UID -eq 0 ]]
then
	echo "Run as root."
	exit
fi

#start first task
./sched_deadline -o0 &
SCH1=$!
./sched_deadline -o10&
SCH2=$!
./sched_deadline -o20&
SCH3=$!


echo "SCHED 1 pid:"
echo $SCH1
#set cpu affinity
taskset  -p 0x01 $SCH1
taskset  -p 0x01 $SCH2
taskset  -p 0x01 $SCH3

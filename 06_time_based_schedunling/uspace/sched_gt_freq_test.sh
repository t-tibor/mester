#!/bin/bash

if [[ $PID -ne 0 ]]
then
	echo "Run as root."
	exit 0
fi

sudo ./sched_gt_test -f2 -o2 -a2 -c1800 &
sudo ./sched_gt_test -f4 -o0 -a1 -c800 &
sudo ./sched_gt_test -f4 -o1 -a1 -c800 &

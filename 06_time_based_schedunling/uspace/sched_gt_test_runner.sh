#!/bin/bash

if [[ $PID -ne 0 ]]
then
	echo "Run as root."
	exit 0
fi

sudo ./sched_gt_test -o5 -a2 -c5 &
sudo ./sched_gt_test -o7 -a2 -c1 &
sudo ./sched_gt_test -o10 -a2 -c1 &
sudo ./sched_gt_test -o15 -a2 -c1 &

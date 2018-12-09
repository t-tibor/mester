#!/bin/bash

if [[ $PID -ne 0 ]]
then
	echo "Run as root."
	exit 0
fi

sudo ./sched_gt_test -f1 -o5 -a2 -c5000 &
sudo ./sched_gt_test -f1 -o7 -a2 -c1500 &
sudo ./sched_gt_test -f1 -o10 -a2 -c1500 &
sudo ./sched_gt_test -f1 -o15 -a2 -c1500 &

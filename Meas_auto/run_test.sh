#!/bin/bash

function print_config()
{
	echo "OFFSET=$o" > ./test_config.sh
	echo "PROC_NUM=$pn" >> ./test_config.sh
	echo "SPREAD=$sp" >> ./test_config.sh
	echo "RT=$r" >> ./test_config.sh
	echo "STRESS_IDX=$s" >> ./test_config.sh
	echo "STRESS_PARAM_1=$STRESS_PARAM_1" >> ./test_config.sh
	echo "STRESS_PARAM_2=$STRESS_PARAM_2" >> ./test_config.sh
	echo "PERIODIC_LOG=$PERIODIC_LOG" >> ./test_config.sh
	echo "TEST_LENGTH=$TEST_LENGTH" >> ./test_config.sh
	#echo "TEST_NAME=$TEST_NAME" >> ./test_config.sh
}

# constants
TEST_APP="/home/debian/test/bin/periodic_rt_task"
source test_parameters.sh

# conecting to the measurment server
exec 10<>/dev/tcp/192.168.137.2/2000


test_idx=0
for o in $OFFSET; do
for pn in $PROC_NUM; do
for sp in $SPREAD; do
for r  in $RT; do
for s in $STRESS_IDX; do

	test_idx=$((test_idx+1))
	echo "----------------------"
	echo "Running test $test_idx"
	echo "----------------------"
	# create folder for the measurement
	test_name="Offset${o}_Proc${pn}_Spread${sp}_Rt${r}_Stress${s}"
	mkdir $test_name
	cd $test_name
	# saving current configuration
	print_config

	# generate test command line arguments
	arg=""
	arg+=" -O ${o} "
	arg+=" -N ${pn} "
	if [ $sp == "ON" ]; then 
		arg+=" -s" 
	fi
	if [ $r == "ON" ]; then
		arg+=" -r"
	fi
	arg+=" -t ${TEST_LENGTH}"

	#start test program
	echo "sudo ${TEST_APP} $arg > periodic.log&"
	sudo ${TEST_APP} $arg > periodic.log&
	PID_TEST=$!
	a=`cat periodic.log | wc -l`
	# wait for the test to start
	while [ $a -lt 1 ]; do
		sleep 0.5
		a=`cat periodic.log | wc -l`
	done


	#setting up stress if neccessary
	if [ $s == 'OFF' ]; then
		echo "No stress used"
	else
		param_name=STRESS_PARAM_$s
		echo "stress-ng ${!param_name}"
		stress-ng ${!param_name} -t $(($TEST_LENGTH+5)) &
		PID_STRESS=$!
	fi

	# wait for stress to start up
	sleep 2
	
	# start sampling
	echo "Starting measurement"
	echo "START ${test_name}" >&10

	#wait for ready
	wait $PID_TEST

	#stop sampling
	echo "Stopping measurement"
	echo "STOP" >&10

	#kill stress
	if [ $s != 'OFF' ]; then
		echo "Killing the stress"
		kill $PID_STRESS
#		sudo kill -9 `ps -au | grep stress-ng | awk '{print $2}'`
	fi
	
	echo "Measurement ready"
	echo ""
	sleep 4
	#exit measurement folder
	cd ..

done
done
done
done
done

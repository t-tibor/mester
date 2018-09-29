BB_CC="/home/tibi/dipterv/bb-kernel-ti-rt/ti-linux-kernel-dev/dl/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc"

all: ./bin/mem_lock ./bin/sched_deadline ./bin/periodic_rt_task ./bin/timer_create


./bin/mem_lock: mem_lock.c
	$(BB_CC) -o ./bin/mem_lock mem_lock.c -pthread

./bin/sched_deadline: sched_deadline.c gpio_uapi.c
	$(BB_CC) -o $@ $^ -lrt -pthread

./bin/periodic_rt_task: periodic_rt_task.c gpio_uapi.c
	$(BB_CC)  -Wall -Wextra -o ./bin/periodic_rt_task  $^ -lrt -pthread

./bin/timer_create: timer_create.c
	$(BB_CC) -o $@ $^  -lrt

.PHONY: all




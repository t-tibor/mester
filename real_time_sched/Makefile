
all: ./bin/mem_lock ./bin/sched_deadline ./bin/periodic_rt_task ./bin/timer_create

./bin/mem_lock: mem_lock.c
	gcc -o ./bin/mem_lock mem_lock.c -pthread

./bin/sched_deadline: sched_deadline.c gpio_uapi.c
	gcc -o $@ $^ -lrt -pthread

./bin/periodic_rt_task: periodic_rt_task.c gpio_uapi.c
	gcc  -Wall -o ./bin/periodic_rt_task  $^ -lrt 

./bin/timer_create: timer_create.c
	gcc -o $@ $^  -lrt

.PHONY: all




BB_CC="/home/tibi/dipterv/bb-kernel-torvalds/dl/gcc-linaro-6.4.1-2017.08-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc"
CFLAGS=-Wall -Wextra -ggdb

.PHONY: all 

all: sched_gt_test good_task bad_task

%.o : %.c
	$(BB_CC) $(CFLAGS)  -c $< -o $@ 

sched_gt_test:  sched_gt_test.o
	$(BB_CC) -o $@  $^ $(CFLAGS)

good_task:  good_task.o
	$(BB_CC) -o $@  $^ $(CFLAGS)

bad_task:  bad_task.o
	$(BB_CC) -o $@  $^ $(CFLAGS)

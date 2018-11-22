#!/bin/bash
CC="/home/tibi/dipterv/bb-kernel-torvalds/dl/gcc-linaro-6.4.1-2017.08-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-"
PERF_PATH="/home/tibi/dipterv/bb-kernel-ti-rt/ti-linux-kernel-dev/KERNEL/tools/perf";

make ARCH=arm CROSS_COMPILE="${CC}" -C $PERF_PATH
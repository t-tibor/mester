#!/bin/bash

COMPILER_PATH="/home/tibi/dipterv/bb-kernel/dl/gcc-linaro-6.4.1-2017.08-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-"

make ARCH=arm CROSS_COMPILE=$COMPILER_PATH

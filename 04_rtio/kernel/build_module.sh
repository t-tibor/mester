#!/bin/bash

CC="/home/tibi/dipterv/bb-kernel-torvalds/dl/gcc-linaro-6.4.1-2017.08-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-"
make ARCH=arm CROSS_COMPILE=${CC}

# copy files back
OUT_PATH="./bin/rt"
find ./ -name *.ko -not -path "./bin/*" -exec cp {} $OUT_PATH \;


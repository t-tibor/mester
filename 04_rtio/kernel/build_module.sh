#!/bin/bash

CC="/home/tibi/dipterv/bb-kernel-torvalds/dl/gcc-linaro-6.4.1-2017.08-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-"

make ARCH=arm CROSS_COMPILE=${CC} normal
OUT_PATH="./bin/normal"
find ./ -name *.ko -not -path "./bin/*" -exec cp {} $OUT_PATH \;

make ARCH=arm CROSS_COMPILE=${CC} rt
OUT_PATH="./bin/rt"
find ./ -name *.ko -not -path "./bin/*" -exec cp {} $OUT_PATH \;

make ARCH=arm CROSS_COMPILE=${CC} xenomai EXTRA_FLAGS="-DUSE_XENOMAI -DECAP_OLD_KERNEL"
OUT_PATH="./bin/xenomai"
find ./ -name *.ko -not -path "./bin/*" -exec cp {} $OUT_PATH \;
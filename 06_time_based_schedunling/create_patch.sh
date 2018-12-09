#!/bin/bash
SOURCE="/home/tibi/dipterv/bb-kernel-torvalds/KERNEL/include/linux/sched.h"
NEW_FILE="/home/tibi/dipterv/bb-kernel-modified/KERNEL/include/linux/sched.h"

diff -Naur $SOURCE $NEW_FILE > kernel_patch.patch
#!/bin/bash

if [ "$(id -u)" != "0" ]; then
echo "This script must be run as root - use sudo"
exit 1
fi

if [ ${#} -eq 0 ]; then
echo "Please give me the kernel version"
exit 1
fi

export kernel_version=${1}

echo "Kernel version: ${kernel_version}"

sudo cp -v ./${kernel_version}.zImage /boot/vmlinuz-${kernel_version} # Copy kernel image
sudo mkdir -p /boot/dtbs/${kernel_version}/
sudo tar xfvo ./${kernel_version}-dtbs.tar.gz -C /boot/dtbs/${kernel_version}/ # Copy kernel DTBs
sudo tar xfvo ./${kernel_version}-modules.tar.gz -C / # Copy kernel modules
sudo tar xfvo ./${kernel_version}-firmware.tar.gz -C /lib/firmware
sudo cp -v ./config-${kernel_version} /boot/

echo "OK! >> Modify the uEnv.txt file with the kernel vesion!"
exit 0
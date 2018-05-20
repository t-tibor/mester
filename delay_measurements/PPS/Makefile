obj-m += pps_device.o

KDIR := /home/tibi/dipterv/bb-kernel/KERNEL

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	

obj-m += irq_lat.o

KDIR-normal:="/home/tibi/dipterv/bb-kernel-torvalds/KERNEL"

all:
	make -C $(KDIR-normal) M=$(PWD) modules

clean:
	make -C $(KDIR-normal) M=$(PWD) clean
	

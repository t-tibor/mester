obj-m += gt_sched.o

KDIR:="/home/tibi/dipterv/bb-kernel-modified/KERNEL"

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	

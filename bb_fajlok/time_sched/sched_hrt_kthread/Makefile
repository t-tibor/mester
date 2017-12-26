obj-m += sched_hrt_kthread.o

KDIR := <4.12.14_kernel_source_path>

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	

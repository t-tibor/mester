obj-m += dmtimer.o
dmtimer-objs := timer_if.o ../icap_channel/icap_channel.o



KDIR := "/home/tibi/dipterv/bb-kernel-torvalds/KERNEL"

KBUILD_CFLAGS  += -DUSE_XENOMAI 


all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	

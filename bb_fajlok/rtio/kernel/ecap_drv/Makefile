obj-m += ecap.o
ecap-objs := ecap_if.o ../icap_channel/icap_channel.o

KDIR := "/home/tibi/dipterv/bb-kernel-torvalds/KERNEL"


all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	

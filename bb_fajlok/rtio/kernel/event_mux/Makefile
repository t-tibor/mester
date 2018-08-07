obj-m += event_mux.o

KDIR := "/home/tibi/dipterv/bb-kernel-torvalds/KERNEL"

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	

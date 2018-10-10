obj-m += my_adc.o
my_adc-objs := my_am335x_adc.o



KDIR := "/home/tibi/dipterv/bb-kernel-torvalds/KERNEL"


all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	

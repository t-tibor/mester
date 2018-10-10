obj-y := event_mux/ DMTimer_drv/ ecap_drv/ ADC_drv/

#KDIR := "/home/tibi/dipterv/bb-kernel-torvalds/KERNEL"
KDIR := "/home/tibi/dipterv/bb-kernel-ti-rt/ti-linux-kernel-dev/KERNEL"


all:
	make -C $(KDIR) M=$(PWD)

clean:
	make -C $(KDIR) M=$(PWD) clean
	

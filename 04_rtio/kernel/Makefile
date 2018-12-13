obj-y := event_mux/ DMTimer_drv/ ecap_drv/


KDIR-normal:="/home/tibi/dipterv/bb-kernel-torvalds/KERNEL"
KDIR-rt:="/home/tibi/dipterv/bb-kernel-ti-rt/ti-linux-kernel-dev/KERNEL"
KDIR-xenomai:="/home/tibi/dipterv/bb-kernel-ti-xenomai/ti-linux-kernel-dev/KERNEL"


.PHONY: all

all:  normal rt xenomai


normal:
	echo "Compiling normal kernel modules."
	make -C $(KDIR-normal) M=$(PWD)

rt:
	echo "Compiling RT kernel modules."
	make -C $(KDIR-rt) M=$(PWD)


# extra flags coming from the command line arguments
KBUILD_CFLAGS += $(EXTRA_FLAGS)
xenomai:
	echo "Compiling Xenomai kernel modules."
	make -C $(KDIR-xenomai) M=$(PWD)


clean:
	make -C $(KDIR) M=$(PWD) clean
	

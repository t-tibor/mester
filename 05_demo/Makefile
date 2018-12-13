BB_CC="/home/tibi/dipterv/bb-kernel-torvalds/dl/gcc-linaro-6.4.1-2017.08-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc"
CC=gcc
CFLAGS=-Wall -Wextra -ggdb
ODIR=bin
LIBS := -lm -lpthread -lrt



RTIO_INCLUDE := ../04_rtio/uspace/include
RTIO_LIB :=  ../04_rtio/uspace/librtio.a

CFLAGS += -I$(RTIO_INCLUDE)


.PHONY: all clean 

all: $(ODIR)/motor_demo $(ODIR)/led_demo

clean:
	rm -f ./*.o
	rm -f ./$(ODIR)/*


%.o : %.c
	$(BB_CC) $(CFLAGS)  -c $< -o $@ $(LIBS)


$(ODIR)/motor_demo : motor_control.o $(RTIO_LIB)
	$(BB_CC) -o $@ $^ $(CFLAGS) $(LIBS)

$(ODIR)/led_demo : led_control.o gpio_uapi.c $(RTIO_LIB)
	$(BB_CC) -o $@ $^ $(CFLAGS) $(LIBS)











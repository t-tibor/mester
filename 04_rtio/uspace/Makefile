BB_CC="/home/tibi/dipterv/bb-kernel-torvalds/dl/gcc-linaro-6.4.1-2017.08-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc"
BB_AR="/home/tibi/dipterv/bb-kernel-torvalds/dl/gcc-linaro-6.4.1-2017.08-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-ar"
CC=gcc
CFLAGS=-Wall -Wextra -ggdb
ODIR=bin


#!!!!! 	set this parameter according to the kernel type !!!!!
# 		it controls the CPTS query period	
CFLAGS+=-DTI_KERNEL


SERVO_TEST_SRC := servo_test.c timekeeper.c ./include/timekeeper.h utils.c ./include/utils.h

INCLUDE_PATH := ./include
ICAP_SRC := icap_channel.c gpio.c timekeeper.c ./PPS_servo/PPS_servo.c  ./PPS_servo/nPPS_servo.c adc.c circ_buf.c utils.c 
SERVO_SRC := ./servo/servo.c ./servo/pi.c ./servo/linreg.c ./servo/print.c circ_buf.c utils.c 
RTIO_OBJECTS := $(patsubst %.c,%.o, $(ICAP_SRC) $(SERVO_SRC) )

CFLAGS += $(patsubst %, -I%, $(INCLUDE_PATH))
LIBS := -lm -lpthread -lrt


.PHONY: all clean 

all: $(ODIR)/event_logger $(ODIR)/PPS_generator $(ODIR)/nPPS_generator  $(ODIR)/servo_test $(ODIR)/PPS_tester


$(ODIR)/servo_test: $(SERVO_TEST_SRC) $(SERVO_SRC)
	$(CC) $(CFLAGS)  -o $@ $^   $(LIBS)



%.o : %.c
	$(BB_CC) $(CFLAGS)  -c $< -o $@ $(LIBS)


librtio.a : $(RTIO_OBJECTS)
	$(BB_AR) rcs $@ $^



$(ODIR)/event_logger:  event_logger.o librtio.a
	$(BB_CC) -o $@  $^ $(CFLAGS) $(LIBS)


$(ODIR)/nPPS_generator: npps_generator.o librtio.a 
	$(BB_CC) -o $@  $^ $(CFLAGS) $(LIBS)

$(ODIR)/PPS_generator:  pps_generator.o librtio.a
	$(BB_CC) -o $@  $^ $(CFLAGS) $(LIBS)


$(ODIR)/PPS_tester: pps_tester.o librtio.a
	$(BB_CC) -o $@  $^ $(CFLAGS) $(LIBS)


clean:
	rm -f ./*.a
	rm -f ./*.o
	rm -f $(ODIR)/*
	rm -f ./servo/*.o

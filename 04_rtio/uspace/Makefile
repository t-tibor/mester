BB_CC="/home/tibi/dipterv/bb-kernel-torvalds/dl/gcc-linaro-6.4.1-2017.08-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc"
CC=gcc
CFLAGS=-Wall -Wextra -ggdb
ODIR=bin


#!!!!! 	set this parameter according to the kernel type !!!!!
# 		it controls the CPTS query period	
#CFLAGS+=-DTI_KERNEL


SERVO_TEST_SRC := servo_test.c timekeeper.c timekeeper.h utils.c utils.h
ICAP_SRC := icap_channel.c icap_channel.h icap_channel_API.h gpio.c gpio.h timekeeper.c timekeeper.h ./PPS_servo/PPS_servo.c  ./PPS_servo/nPPS_servo.c adc.c adc.h circ_buf.h circ_buf.c utils.c utils.h
SERVO_SRC := ./servo/servo.c ./servo/pi.c ./servo/linreg.c ./servo/print.c circ_buf.c circ_buf.h utils.c utils.h
LIBS := -lm -lpthread -lrt


.PHONY: all clean 

all: $(ODIR)/event_logger $(ODIR)/PPS_generator $(ODIR)/nPPS_generator  $(ODIR)/servo_test $(ODIR)/PPS_tester


%.o : %.c
	$(CC) $(CFLAGS)  -c $< -o $@ $(LIBS)


$(ODIR)/servo_test: $(SERVO_TEST_SRC) $(SERVO_SRC)
	$(CC) $(CFLAGS)  -o $@ $^   $(LIBS)


$(ODIR)/event_logger: $(ICAP_SRC)  $(SERVO_SRC) event_logger.c
	$(BB_CC) $(CFLAGS) -o $@  $^  $(LIBS) 


$(ODIR)/nPPS_generator: $(ICAP_SRC)  $(SERVO_SRC) npps_generator.c
	$(BB_CC) $(CFLAGS) -o $@  $^   $(LIBS) 

$(ODIR)/PPS_generator: $(ICAP_SRC)  $(SERVO_SRC) pps_generator.c
	$(BB_CC) $(CFLAGS) -o $@  $^   $(LIBS) 

$(ODIR)/PPS_tester: $(ICAP_SRC)  $(SERVO_SRC) pps_tester.c
	$(BB_CC) $(CFLAGS)  -o $@  $^  $(LIBS)


clean:
	rm -f $(ODIR)/*
	rm -f ./servo/*.o
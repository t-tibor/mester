BB_CC="/home/tibi/dipterv/bb-kernel-torvalds/dl/gcc-linaro-6.4.1-2017.08-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc"
CC=gcc
CFLAGS=-Wall -Wextra -ggdb
ODIR=bin


SERVO_TEST_SRC := servo_test.c timekeeper.c timekeeper.h
ICAP_SRC := icap_channel.c icap_channel.h gpio.c gpio.h timekeeper.c timekeeper.h
SERVO_SRC := ./servo/servo.c ./servo/pi.c ./servo/linreg.c ./servo/print.c
LIBS := -lm -lpthread

%.o : %.c
	$(CC) $(CFLAGS)  -c $< -o $@ $(LIBS)


$(ODIR)/servo_test: $(SERVO_TEST_SRC) $(SERVO_SRC)
	$(CC) $(CFLAGS)  -o $@ $^   $(LIBS)

$(ODIR)/icap_channel: $(ICAP_SRC)  $(SERVO_SRC)
	$(BB_CC) $(CFLAGS) $(LIBS) -o $@  $^ 

.PHONY: all clean 


all: $(ODIR)/icap_channel $(ODIR)/servo_test

clean:
	rm -f $(ODIR)/*
	rm -f ./servo/*.o
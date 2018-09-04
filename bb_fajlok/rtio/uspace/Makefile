CC=gcc  #"/home/tibi/dipterv/bb-kernel-torvalds/dl/gcc-linaro-6.4.1-2017.08-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc"

CFLAGS=-Wall -ggdb
ODIR=bin


SERVO_TEST_SRC := servo_test.c
ICAP_SRC := icap_channel.c icap_channel.h gpio.c gpio.h
SERVO_OBJ := ./servo/servo.o ./servo/pi.o ./servo/linreg.o ./servo/print.o
LIBS := -lm

%.o : %.c
	$(CC) $(CFLAGS)  -c $< -o $@ $(LIBS)


$(ODIR)/servo_test: $(SERVO_TEST_SRC) $(SERVO_OBJ)
	$(CC) $(CFLAGS)  -o $@ $^   $(LIBS)

$(ODIR)/icap_channel: $(ICAP_SRC) $(SERVO_OBJ)
	$(CC) $(CFLAGS) $(LIBS) -o $@  $^ 

.PHONY: all clean 


all: $(ODIR)/icap_channel $(ODIR)/servo_test

clean:
	rm -f $(ODIR)/*
	rm -f ./servo/*.o
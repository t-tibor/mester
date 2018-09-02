CC="/home/tibi/dipterv/bb-kernel-torvalds/dl/gcc-linaro-6.4.1-2017.08-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc"
CFLAGS=-Wall -ggdb
ODIR=bin

ICAP_SRC := icap_channel.c icap_channel.h gpio.c gpio.h


$(ODIR)/icap_channel: $(ICAP_SRC)
	$(CC) $(CFLAGS) -o $@  $^ 

.PHONY: clean


all: $(ODIR)/icap_channel

clean:
	rm $(ODIR)/*
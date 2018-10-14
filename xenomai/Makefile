SKIN=alchemy

MAIN_SRC=periodic_xeno_task.c gpio_uapi.c gpio_uapi.h 

TARGET=periodic_xeno_task

LM=-lm

XENO_CONFIG := /usr/xenomai/bin/xeno-config
CFLAGS := $(shell $(XENO_CONFIG) --skin=alchemy --cflags)
LDFLAGS := $(LM) $(shell $(XENO_CONFIG) --skin=alchemy  --ldflags)
CC := $(shell $(XENO_CONFIG) --cc)

$(TARGET): $(MAIN_SRC)
	$(CC) -o $@ $^ -Wall -Wextra $(CFLAGS) $(LDFLAGS)
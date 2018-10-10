SKIN=alchemy
MAIN_SRC=periodic_task
TARGET=periodic_task

LM=-lm

XENO_CONFIG := /usr/xenomai/bin/xeno-config
CFLAGS := $(shell $(XENO_CONFIG) --skin=alchemy --cflags)
LDFLAGS := $(LM) $(shell $(XENO_CONFIG) --skin=alchemy --ldflags)
CC := $(shell $(XENO_CONFIG) --cc)

$(TARGET): $(MAIN_SRC).c
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)
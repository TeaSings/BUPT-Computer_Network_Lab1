CC = gcc
CFLAGS = -O2 -Wall -I.
LDLIBS = -lm

PROTOCOL ?= sr
PROTO_SRC = protocols/$(PROTOCOL).c
COMMON_SRC = protocol.c lprintf.c crc32.c

.PHONY: all sr gbn clean list

all: datalink

datalink: $(PROTO_SRC) $(COMMON_SRC) datalink.h protocol.h lprintf.h
	$(CC) $(CFLAGS) $(PROTO_SRC) $(COMMON_SRC) -o $@ $(LDLIBS)

sr:
	$(MAKE) PROTOCOL=sr datalink

gbn:
	$(MAKE) PROTOCOL=gbn datalink

list:
	@echo "Available protocols: sr gbn"

clean:
	$(RM) datalink *.o protocols/*.o *.log

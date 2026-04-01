CC      ?= cc
CFLAGS  = -O2 -Wall

SLCAN_TARGETS = slcan_reader slcan_writer
NET_TARGETS   = net_reader net_writer

all: $(SLCAN_TARGETS) $(NET_TARGETS)

slcan: $(SLCAN_TARGETS)

net: $(NET_TARGETS)

slcan_reader: slcan_reader.c
	$(CC) $(CFLAGS) -o $@ $<

slcan_writer: slcan_writer.c
	$(CC) $(CFLAGS) -o $@ $<

net_reader: net_reader.c
	$(CC) $(CFLAGS) -o $@ $<

net_writer: net_writer.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(SLCAN_TARGETS) $(NET_TARGETS)

.PHONY: all slcan net clean

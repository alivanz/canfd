KVERSION ?= $(shell uname -r)
CC       ?= gcc
CFLAGS   := -O2 -Wall

TOOLS := net_reader net_writer slcan_reader slcan_writer \
         sange_can sange_reader sange_writer test_single_frame

.PHONY: all tools drivers clean

all: tools drivers

tools: $(TOOLS)

%: %.c cants.h
	$(CC) $(CFLAGS) -o $@ $<

sange_can sange_reader sange_writer: %: %.c sange.h cants.h
	$(CC) $(CFLAGS) -o $@ $<

drivers:
	$(MAKE) -C ch36x_linux/driver
	$(MAKE) -C f81601_driver
	$(MAKE) -C fbus_driver
	$(MAKE) -C SDCLinuxExpansionBoardDriver_V2.1.4.0/driver

clean:
	rm -f $(TOOLS)
	$(MAKE) -C ch36x_linux/driver clean
	$(MAKE) -C f81601_driver clean
	$(MAKE) -C fbus_driver clean
	$(MAKE) -C SDCLinuxExpansionBoardDriver_V2.1.4.0/driver clean

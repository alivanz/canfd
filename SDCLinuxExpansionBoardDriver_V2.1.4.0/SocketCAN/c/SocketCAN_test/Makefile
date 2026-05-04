# SPDX-License-Identifier: GPL-2.0
PWD := $(shell pwd)

LDFLAGS=

GCC_C=gcc -Wall -g -c
GCC_O=gcc $(LDFLAGS) -g -o

all:socketcan_test

clean:
	rm -f *~ 
	rm -f obj/socketcan_test

socketcan_test:
	$(GCC_C) main.c
	$(GCC_C) utils.c
	$(GCC_C) socketcan_func_test.c
	mv -f *.o obj

	$(GCC_O) socketcan_test \
	obj/main.o \
	obj/utils.o \
	obj/socketcan_func_test.o \
	-L obj -pthread

	rm -f obj/*.o
	mv -f socketcan_test obj

# SPDX-License-Identifier: GPL-2.0
PWD := $(shell pwd)



GCC_C=gcc -Wall -g -c

LDFLAGS= -lgpiod -pthread
GCC_O = gcc -g -o
#GCC_O=gcc $(LDFLAGS) -g -o



all:gpio_test

clean:
	rm -f *~ 
	rm -f obj/gpio_test
	
gpio_test:
	$(GCC_C) main.c
	$(GCC_C) gpio_test.c
	$(GCC_C) utils.c
	mv -f *.o obj
	
	$(GCC_O) gpio_test \
	obj/main.o \
	obj/gpio_test.o \
	obj/utils.o \
	$(LDFLAGS)
	
	rm -f obj/*.o
	mv -f gpio_test obj

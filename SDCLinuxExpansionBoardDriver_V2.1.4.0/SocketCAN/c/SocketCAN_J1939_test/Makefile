PWD := $(shell pwd)

LDFLAGS=

GCC_C=gcc -Wall -g -c
GCC_O=gcc $(LDFLAGS) -g -o

all:J1939_test

clean:
	rm -f *~ *.o
	rm -f obj/J1939_test

J1939_test:
	$(GCC_C) main.c
	$(GCC_C) utils.c
	$(GCC_C) J1939_test.c
	mv -f *.o obj

	$(GCC_O) J1939_test \
	obj/main.o \
	obj/utils.o \
	obj/J1939_test.o \
	-L obj -pthread

	rm -f obj/*.o
	mv -f J1939_test obj

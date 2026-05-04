# SPDX-License-Identifier: MIT
#
# SDC CARD config program
#
# Copyright (c) 2025 SUNIX Co., Ltd. <info@sunix.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

PWD = $(shell pwd)
GCC_C = gcc -c -O2 -Wall
GCC_O = gcc -o

all:lib sdc-config

lib:
	cd ../lib;\
	make clean;\
	make;\
	cd $(PWD)

sdc-config:
	$(GCC_C) board.c
	$(GCC_C) config_dio.c
	$(GCC_C) config_firmware.c
	$(GCC_C) driver.c
	$(GCC_C) tty.c
	$(GCC_C) main.c
	mv -f *.o obj

	$(GCC_O) sdc-config \
	obj/board.o \
	obj/config_dio.o \
	obj/config_firmware.o \
	obj/driver.o \
	obj/tty.o \
	obj/main.o \
	-L obj -L../lib/obj -lsdcboard

	rm -f obj/*.o obj/*.a
	mv -f sdc-config obj

clean:
	rm -f *~ *.o
	rm -f obj/*

install:
	@echo "## COPY file"
	sudo cp obj/sdc-config /usr/sbin
	@echo "## DONE"

uninstall:
	@echo "## REMOVE file"
	sudo rm -f /usr/sbin/sdc-config
	@echo "## DONE"

# SPDX-License-Identifier: MIT
#
# SDC library
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
LIB_REAL_NAME = libsdc.so.2.0.0
LIB_SO_NAME = libsdc.so.2
LIB_LINK_NAME = libsdc.so
GCC_C = gcc -c -O2 -Wall -fPIC

all:sdclib

sdclib:
	$(GCC_C) dio_list.c
	$(GCC_C) dio_main.c
	$(GCC_C) dio_rx_thd.c
	$(GCC_C) main.c
	mv -f *.o obj

	gcc -fPIC -g -shared -Wl,-soname,$(LIB_SO_NAME) -o $(LIB_REAL_NAME) \
	obj/dio_list.o \
	obj/dio_main.o \
	obj/dio_rx_thd.o \
	obj/main.o \
	-L obj -pthread

	rm -f obj/*
	mv -f $(LIB_REAL_NAME) obj

clean:
	rm -f *~ *.o *.so.*
	rm -f obj/*

install:
	@echo "## COPY file"
	sudo cp obj/$(LIB_REAL_NAME) /usr/lib
	@echo "## CALL ldconfig"
	sudo ldconfig
	@echo "## DONE"

uninstall:
	@echo "## REMOVE file"
	sudo rm -f /usr/lib/$(LIB_REAL_NAME)
	@echo "## CALL ldconfig"
	sudo ldconfig
	@echo "## DONE"

# SPDX-License-Identifier: MIT
#
# SDC library test program
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

all:sdclibtest

sdclibtest:
	$(GCC_C) dio_callback.c
	$(GCC_C) dio_test.c
	$(GCC_C) utils.c
	$(GCC_C) main.c
	mv -f *.o obj

	$(GCC_O) sdclibtest \
	obj/dio_callback.o \
	obj/dio_test.o \
	obj/utils.o \
	obj/main.o \
	-L obj -l:libsdc.so.2

	rm -f obj/*.o
	mv -f sdclibtest obj

clean:
	rm -f *~ *.o
	rm -f obj/*

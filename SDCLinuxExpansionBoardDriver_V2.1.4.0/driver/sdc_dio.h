/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sdc_dio - SUNIX SDC DIO controller driver
 *
 * Copyright (c) 2025 SUNIX Co., Ltd. <info@sunix.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 *
 */

#ifndef _SDC_DIO_H_
#define _SDC_DIO_H_

#include "sunix-sdc.h"

#define DIO_BANK_IO_MAX			32
#define DIO_BANK_MAX			32

struct dio_bank {
	__u32 bank_idx; // _In_
	__u32 value; // _Inout_
};

struct dio_bank_input_counter {
	__u32 bank_idx; // _In_
	__u32 nr_io; // _Out_
	__u32 value[DIO_BANK_IO_MAX]; // _Out_
};

struct dio_bank_io {
	__u32 bank_idx; // _In_
	__u32 io_idx; // _In_
	__u32 value; // _Inout_
};

struct dio_read {
	__u32 input;
	__u32 input_delta;
};

struct dio_bank_info {
	int nr_io;
	int capability;
	int cap_input;
	int cap_output;
	int cap_rising_trigger;
	int cap_falling_trigger;
	int io_mask;
	int io_shift_bits;
};

struct dio_info {
	int pci_bus;
	int irq;
	int line;

	int index;
	int version;
	int capability;
	int cap_samedirection;
	int cap_storeflash;
	int nr_bank;

	int di_sampling_freq;
	int di_filter_lower_bound;
	int di_filter_min;
	int di_filter_max;

	struct dio_bank_info banks[DIO_BANK_MAX];
};

#define DIO_GET_INFO		_IOR('d', 1, struct dio_info *)
#define DIO_GET_BANK_DIRECTION \
							_IOWR('d', 3, struct dio_bank *)
#define DIO_SET_BANK_DIRECTION \
							_IOW('d', 4, struct dio_bank *)
#define DIO_GET_BANK_STATE	_IOWR('d', 5, struct dio_bank *)
#define DIO_SET_BANK_STATE	_IOW('d', 6, struct dio_bank *)
#define DIO_GET_BANK_INPUT_INVERT \
							_IOWR('d', 7, struct dio_bank *)
#define DIO_SET_BANK_INPUT_INVERT \
							_IOW('d', 8, struct dio_bank *)
#define DIO_GET_BANK_INPUT_LATCH_POSITIVE \
							_IOWR('d', 9, struct dio_bank *)
#define DIO_SET_BANK_INPUT_LATCH_POSITIVE \
							_IOW('d', 10, struct dio_bank *)
#define DIO_GET_BANK_INPUT_LATCH_NEGATIVE \
							_IOWR('d', 11, struct dio_bank *)
#define DIO_SET_BANK_INPUT_LATCH_NEGATIVE \
							_IOW('d', 12, struct dio_bank *)
#define DIO_GET_BANK_INPUT_COUNTER_INCREMENT_POSITIVE \
							_IOWR('d', 13, struct dio_bank *)
#define DIO_SET_BANK_INPUT_COUNTER_INCREMENT_POSITIVE \
							_IOW('d', 14, struct dio_bank *)
#define DIO_GET_BANK_INPUT_COUNTER_INCREMENT_NEGATIVE \
							_IOWR('d', 15, struct dio_bank *)
#define DIO_SET_BANK_INPUT_COUNTER_INCREMENT_NEGATIVE \
							_IOW('d', 16, struct dio_bank *)
#define DIO_GET_BANK_INPUT_EVENT_CTRL_RISING \
							_IOWR('d', 17, struct dio_bank *)
#define DIO_SET_BANK_INPUT_EVENT_CTRL_RISING \
							_IOW('d', 18, struct dio_bank *)
#define DIO_GET_BANK_INPUT_EVENT_CTRL_FALLING \
							_IOWR('d', 19, struct dio_bank *)
#define DIO_SET_BANK_INPUT_EVENT_CTRL_FALLING \
							_IOW('d', 20, struct dio_bank *)
#define DIO_GET_BANK_OUTPUT_INITIAL_VALUE \
							_IOWR('d', 21, struct dio_bank *)
#define DIO_SET_BANK_OUTPUT_INITIAL_VALUE \
							_IOW('d', 22, struct dio_bank *)
#define DIO_SET_BANK_INPUT_COUNTER_RESET \
							_IOW('d', 23, struct dio_bank *)
#define DIO_GET_BANK_INPUT_COUNTER_VALUE \
							_IOWR('d', 24, struct dio_bank_input_counter *)
#define DIO_GET_BANK_INPUT_IO_FILTER \
							_IOWR('d', 25, struct dio_bank_io *)
#define DIO_SET_BANK_INPUT_IO_FILTER \
							_IOW('d', 26, struct dio_bank_io *)

#endif

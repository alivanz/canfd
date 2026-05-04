/* SPDX-License-Identifier: MIT */
/*
 * SDC library
 *
 * Copyright (c) 2025 SUNIX Co., Ltd. <info@sunix.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _SDC_GLOBAL_H_
#define _SDC_GLOBAL_H_

#define ENABLE_TRACE_LIBRARY		0

#if ENABLE_TRACE_LIBRARY
#define XPRINT(fmt, ...)			printf("%s " fmt, "[LIB]", __VA_ARGS__)
#else
#define XPRINT(...)
#endif

#define THD_STACK_SIZE				65536

/* dio */
struct dio_ex {
	struct xlist entry;
	int line;
	int fd;
	struct dio_info drvinfo;
	int di_filter_lower_bound_ms;
	int di_filter_min_ms;
	int di_filter_max_ms;

	pthread_mutex_t callback_lock;
	int callback_bank_idx;
	sdc_dio_callback callback_ptr;

	int rxthd_create;
	int rxthd_exit_fd;
	pthread_t rxthd_hd;
	pthread_attr_t rxthd_attr;
	int rxthd_wait_timeout;
	unsigned long rxthd_event_count;
};

#define DIO_EX_PTR(list) \
	CONTAIN_OF(list, struct dio_ex, entry)

/* global */
extern struct xlist GB_dio_ex_list;
extern int GB_nr_of_dio_ex;

#endif

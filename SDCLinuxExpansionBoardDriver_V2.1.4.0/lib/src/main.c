// SPDX-License-Identifier: MIT
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

#include "precomp.h"

struct xlist GB_dio_ex_list;
int GB_nr_of_dio_ex;

static void __attribute__ ((constructor)) lib_init(void);
static void __attribute__ ((destructor)) lib_free(void);

void lib_init(void)
{
	XPRINT("----> %s()\n", __func__);

	xlist_init(&GB_dio_ex_list);
	GB_nr_of_dio_ex = 0;

	XPRINT("<---- %s\n", __func__);
}

void lib_free(void)
{
	XPRINT("----> %s()\n", __func__);

	dio_terminate_all();

	XPRINT("<---- %s\n", __func__);
}

int sdc_dio_open(int line)
{
	return dio_open(line);
}

int sdc_dio_close(int line)
{
	return dio_close(line);
}

int sdc_dio_get_info(int line, struct sdc_dio_info *info)
{
	return dio_get_info(line, info);
}

int sdc_dio_get_bank_direction(int line, int bank_idx, int *value)
{
	return dio_get_bank_direction(line, bank_idx, value);
}

int sdc_dio_set_bank_direction(int line, int bank_idx, int value)
{
	return dio_set_bank_direction(line, bank_idx, value);
}

int sdc_dio_get_bank_state(int line, int bank_idx, int *value)
{
	return dio_get_bank_state(line, bank_idx, value);
}

int sdc_dio_set_bank_state(int line, int bank_idx, int value)
{
	return dio_set_bank_state(line, bank_idx, value);
}

int sdc_dio_get_bank_input_invert(int line, int bank_idx, int *value)
{
	return dio_get_bank_input_invert(line, bank_idx, value);
}

int sdc_dio_set_bank_input_invert(int line, int bank_idx, int value)
{
	return dio_set_bank_input_invert(line, bank_idx, value);
}

int sdc_dio_get_bank_input_latch_positive_edge(int line, int bank_idx, int *value)
{
	return dio_get_bank_input_latch_positive_edge(line, bank_idx, value);
}

int sdc_dio_set_bank_input_latch_positive_edge(int line, int bank_idx, int value)
{
	return dio_set_bank_input_latch_positive_edge(line, bank_idx, value);
}

int sdc_dio_get_bank_input_latch_negative_edge(int line, int bank_idx, int *value)
{
	return dio_get_bank_input_latch_negative_edge(line, bank_idx, value);
}

int sdc_dio_set_bank_input_latch_negative_edge(int line, int bank_idx, int value)
{
	return dio_set_bank_input_latch_negative_edge(line, bank_idx, value);
}

int sdc_dio_get_bank_input_counter_increment_positive_edge(int line, int bank_idx, int *value)
{
	return dio_get_bank_input_counter_increment_positive_edge(line, bank_idx, value);
}

int sdc_dio_set_bank_input_counter_increment_positive_edge(int line, int bank_idx, int value)
{
	return dio_set_bank_input_counter_increment_positive_edge(line, bank_idx, value);
}

int sdc_dio_get_bank_input_counter_increment_negative_edge(int line, int bank_idx, int *value)
{
	return dio_get_bank_input_counter_increment_negative_edge(line, bank_idx, value);
}

int sdc_dio_set_bank_input_counter_increment_negative_edge(int line, int bank_idx, int value)
{
	return dio_set_bank_input_counter_increment_negative_edge(line, bank_idx, value);
}

int sdc_dio_get_bank_input_event_ctrl(int line, int bank_idx, int *rising, int *falling)
{
	return dio_get_bank_input_event_ctrl(line, bank_idx, rising, falling);
}

int sdc_dio_set_bank_input_event_ctrl(int line, int bank_idx, int rising, int falling)
{
	return dio_set_bank_input_event_ctrl(line, bank_idx, rising, falling);
}

int sdc_dio_get_bank_output_initial_value(int line, int bank_idx, int *value)
{
	return dio_get_bank_output_initial_value(line, bank_idx, value);
}

int sdc_dio_set_bank_output_initial_value(int line, int bank_idx, int value)
{
	return dio_set_bank_output_initial_value(line, bank_idx, value);
}

int sdc_dio_set_bank_input_counter_reset(int line, int bank_idx, int port_idx)
{
	return dio_set_bank_input_counter_reset(line, bank_idx, port_idx);
}

int sdc_dio_get_bank_input_counter_value(int line, int bank_idx, int port_idx, int *value)
{
	return dio_get_bank_input_counter_value(line, bank_idx, port_idx, value);
}

int sdc_dio_get_bank_input_filter_value(int line, int bank_idx, int port_idx, int *value)
{
	return dio_get_bank_input_filter_value(line, bank_idx, port_idx, value);
}

int sdc_dio_set_bank_input_filter_value(int line, int bank_idx, int port_idx, int value)
{
	return dio_set_bank_input_filter_value(line, bank_idx, port_idx, value);
}

int sdc_dio_register_event_callback(int line, sdc_dio_callback callback_ptr)
{
	return dio_register_event_callback(line, callback_ptr);
}

int sdc_dio_unregister_event_callback(int line)
{
	return dio_unregister_event_callback(line);
}

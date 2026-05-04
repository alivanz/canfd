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

#ifndef _SDC_FUNCTION_H_
#define _SDC_FUNCTION_H_

/* dio_list.c */
struct dio_ex *dio_create_one(int line, int fd);
void dio_terminate_one(int line);
void dio_terminate_all(void);
struct dio_ex *dio_get_by_line(int line);

/* dio_main.c */
int dio_open(int line);
int dio_close(int line);
int dio_get_info(int line, struct sdc_dio_info *info);
int dio_get_bank_direction(int line, int bank_idx, int *value);
int dio_set_bank_direction(int line, int bank_idx, int value);
int dio_get_bank_state(int line, int bank_idx, int *value);
int dio_set_bank_state(int line, int bank_idx, int value);
int dio_get_bank_input_invert(int line, int bank_idx, int *value);
int dio_set_bank_input_invert(int line, int bank_idx, int value);
int dio_get_bank_input_latch_positive_edge(int line, int bank_idx, int *value);
int dio_set_bank_input_latch_positive_edge(int line, int bank_idx, int value);
int dio_get_bank_input_latch_negative_edge(int line, int bank_idx, int *value);
int dio_set_bank_input_latch_negative_edge(int line, int bank_idx, int value);
int dio_get_bank_input_counter_increment_positive_edge(int line, int bank_idx, int *value);
int dio_set_bank_input_counter_increment_positive_edge(int line, int bank_idx, int value);
int dio_get_bank_input_counter_increment_negative_edge(int line, int bank_idx, int *value);
int dio_set_bank_input_counter_increment_negative_edge(int line, int bank_idx, int value);
int dio_get_bank_input_event_ctrl(int line, int bank_idx, int *rising, int *falling);
int dio_set_bank_input_event_ctrl(int line, int bank_idx, int rising, int falling);
int dio_get_bank_output_initial_value(int line, int bank_idx, int *value);
int dio_set_bank_output_initial_value(int line, int bank_idx, int value);
int dio_set_bank_input_counter_reset(int line, int bank_idx, int port_idx);
int dio_get_bank_input_counter_value(int line, int bank_idx, int port_idx, int *value);
int dio_get_bank_input_filter_value(int line, int bank_idx, int port_idx, int *value);
int dio_set_bank_input_filter_value(int line, int bank_idx, int port_idx, int value);
int dio_register_event_callback(int line, sdc_dio_callback callback_ptr);
int dio_unregister_event_callback(int line);

/* dio_rx_thd.c */
int dio_rxthd_create(struct dio_ex *ex);
void dio_rxthd_terminate(struct dio_ex *ex);

#endif

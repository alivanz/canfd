/* SPDX-License-Identifier: MIT */
/*
 * SDC library test program
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

// dio_callback.c
void dio_callback(int line, int bank_idx, int input, int input_delta);

// dio_test.c
void dio_test(void);

// utils.c
char sgetche(void);
__u8 input_u8_value(void);
__u16 input_u16_value(void);
__u32 input_u32_value(void);
int input_dio_line(void);
int input_dio_get_or_set(void);
int input_dio_bank_index(void);
int input_dio_bank_port_index(void);
int input_dio_rising_event_ctrl(void);
int input_dio_falling_event_ctrl(void);
int input_dio_filter_value(void);

#endif

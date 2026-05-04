/* SPDX-License-Identifier: MIT */
/*
 * SDC CARD config program
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

/* board.c */
int board_display_all(void);
int board_display_detail(char *parm1);

/* config_dio.c */
int config_dio_store(int quiet, char *parm1, int nr_reg, char **regs);
int config_dio_usage(char *prog);

/* config_firmware.c */
int config_firmware_update(char *parm1, char *parm2);
int config_firmware_usage(char *prog);

/* driver.c */
int driver_version(void);

/* tty.c */
int tty_get_rs485_config(char *parm1);
int tty_set_rs485_config(char *prog, char *parm1, char *parm2, char *parm3);
int tty_usage(char *prog);

#endif

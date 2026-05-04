/**
 * @file precomp.h
 * @brief cmd list for test GPIO function
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2025, SUNIX Co., Ltd.
 *
 * Authors      : Max Chang <max.chang@sunix.com>
 * 
 * 
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <gpiod.h>
#include <fcntl.h>
#include <pthread.h>
#include <dirent.h>
#include <inttypes.h> 
#include <linux/types.h>

// gpio_test.c
void gpio_test(void);

// utils.c
char sgetche(void);
int input_gpio_device_number(void);
int input_line_number(void);
int input_gpio_output_level_value(void);
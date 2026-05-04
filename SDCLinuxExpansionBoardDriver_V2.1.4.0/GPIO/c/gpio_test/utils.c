/**
 * @file utils.c
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

#include "precomp.h"

static int _check_and_convert(char *data, int len, __u8 *u8_ptr, __u16 *u16_ptr, __u32 *u32_ptr)
{
	unsigned int hi_b = 0, low_b = 0;
	__u8 u8_d = 0;
	__u16 u16_d = 0;
	__u32 u32_d = 0;
	int i, j;

	if (len != 2 && len != 4 && len != 8)
		return -1;

	if (strlen(data) != len)
		return -1;

	for (i = 0; i < len; i++) {
		if (((data[i] < '0') || (data[i] > '9')) &&
			((data[i] < 'a') || (data[i] > 'f')) &&
			((data[i] < 'A') || (data[i] > 'F')))
			return -1;

		data[i] = (char)tolower((int)data[i]);
	}

	switch (len) {
	case 2:
		if (u8_ptr) {
			for (i = 0, j = 0; i < 2; i += 2, j++) {
				hi_b = data[i] > '9' ? data[i] - 'a' + 10 : data[i] - '0';
				low_b = data[i + 1] > '9' ? data[i + 1] - 'a' + 10 : data[i + 1] - '0';
				u8_d = (hi_b << 4) | low_b;
			}
			*u8_ptr = u8_d;
		}
		break;
	case 4:
		if (u16_ptr) {
			for (i = 0, j = 0; i < 4; i += 2, j++) {
				hi_b = data[i] > '9' ? data[i] - 'a' + 10 : data[i] - '0';
				low_b = data[i + 1] > '9' ? data[i + 1] - 'a' + 10 : data[i + 1] - '0';
				u16_d |= ((hi_b << 4) | low_b) << (8 - (i * 4));
			}
			*u16_ptr = u16_d;
		}
		break;
	case 8:
		if (u32_ptr) {
			for (i = 0, j = 0; i < 8; i += 2, j++) {
				hi_b = data[i] > '9' ? data[i] - 'a' + 10 : data[i] - '0';
				low_b = data[i + 1] > '9' ? data[i + 1] - 'a' + 10 : data[i + 1] - '0';
				u32_d |= ((hi_b << 4) | low_b) << (24 - (i * 4));
			}
			*u32_ptr = u32_d;
		}
		break;
	}

	return 0;
}

char sgetche(void)
{
	struct termios old_term;
	struct termios new_term;
	char result, read_ret;
	int length;

	tcgetattr(0, &old_term);
	memcpy(&new_term, &old_term, sizeof(struct termios));
	new_term.c_lflag &= ~(ICANON | ECHO);

	tcsetattr(0, TCSANOW, &new_term);
	length = read(0, &read_ret, 1);
	tcsetattr(0, TCSANOW, &old_term);

	if (length == 1)
		result = read_ret;
	else
		result = 0;

	return result;
}

__u8 input_u8_value(void)
{
	char data[256];
	__u8 result = 0;

	while (1) {
		memset(data, 0, sizeof(data));
		printf("Value (1 byte)                        ? x");
		(void)!scanf(" %s", data);

		if (_check_and_convert(data, 2, &result, NULL, NULL) == 0)
			break;
	}

	return result;
}

__u16 input_u16_value(void)
{
	char data[256];
	__u16 result = 0;

	while (1) {
		memset(data, 0, sizeof(data));
		printf("Value (2 byte)                        ? x");
		(void)!scanf(" %s", data);

		if (_check_and_convert(data, 4, NULL, &result, NULL) == 0)
			break;
	}

	return result;
}

__u32 input_u32_value(void)
{
	char data[256];
	__u32 result = 0;

	while (1) {
		memset(data, 0, sizeof(data));
		printf("Value (4 byte)                        ? x");
		(void)!scanf(" %s", data);

		if (_check_and_convert(data, 8, NULL, NULL, &result) == 0)
			break;
	}

	return result;
}

int input_gpio_device_number(void)
{
	int result;

	printf("GPIO device number(e.g if /dev/gpiochip1, input 1)                       ? ");
	(void)!scanf(" %d", &result);

	return result;
}

int input_line_number(void)
{
	int result;

	printf("GPIO chip line number                       ? ");
	(void)!scanf(" %d", &result);

	return result;
}

int input_gpio_output_level_value(void)
{
	int result;

	printf("Output value(0 or 1)                       ? ");
	
	while (scanf("%d", &result) != 1 || (result != 0 && result != 1)) {
        printf("Invalid number, input again(0/1):");
        while (getchar() != '\n');
    }

	return result;
}












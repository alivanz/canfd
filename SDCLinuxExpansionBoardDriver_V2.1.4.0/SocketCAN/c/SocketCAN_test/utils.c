/**
 * @file utils.c
 * @brief utils function
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2022, SUNIX Co., Ltd.
 *
 * Authors      : Andy Jheng <andy_jheng@sunix.com>
 * 
 * 
**/

#include "precomp.h"

/*****************************************************************************************
 basic utils 
*****************************************************************************************/

static int check_and_convert(char *data, int len, __u8 *u8_ptr,	__u16 *u16_ptr,
								__u32 *u32_ptr)
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
				low_b = data[i + 1] > '9' ? data[i + 1] - 'a' + 10 :
											data[i + 1] - '0';
				u8_d = (hi_b << 4) | low_b;
			}
			*u8_ptr = u8_d;
		}
		break;
	case 4:
		if (u16_ptr) {
			for (i = 0, j = 0; i < 4; i += 2, j++) {
				hi_b = data[i] > '9' ? data[i] - 'a' + 10 : data[i] - '0';
				low_b = data[i + 1] > '9' ? data[i + 1] - 'a' + 10 :
											data[i + 1] - '0';
				u16_d |= ((hi_b << 4) | low_b) << (8 - (i * 4));
			}
			*u16_ptr = u16_d;
		}
		break;
	case 8:
		if (u32_ptr) {
			for (i = 0, j = 0; i < 8; i += 2, j++) {
				hi_b = data[i] > '9' ? data[i] - 'a' + 10 : data[i] - '0';
				low_b = data[i + 1] > '9' ? data[i + 1] - 'a' + 10 :
											data[i + 1] - '0';
				u32_d |= ((hi_b << 4) | low_b) << (24 - (i * 4));
			}
			*u32_ptr = u32_d;
		}
		break;
	default:
		return -1;
	}

	return 0;
}

/*-----------------------------------------------------------------------------*/

void print_l(int button)
{
	if (button)
		printf("-----------------------------------------------\n\n");
	else
		printf("-----------------------------------------------\n");
}

/*-----------------------------------------------------------------------------*/

char sgetche(void)
{
	struct termios old_term;
	struct termios new_term;
	char result;
	int length;

	tcgetattr(0, &old_term);
	memcpy(&new_term, &old_term, sizeof(struct termios));
	new_term.c_lflag &= ~(ICANON | ECHO);

	tcsetattr(0, TCSANOW, &new_term);
	length = read(0, &result, 1);
	tcsetattr(0, TCSANOW, &old_term);

	if (length == 1)
		return result;

	return 0;
}

/*****************************************************************************************
 SocketCAN utils input
*****************************************************************************************/

// user input enable or disable when write messag
int input_msg_user_input(void)
{
	int result;

	do {
		printf("d[#_#]b : 0->no, 1->yes\n");
		printf("d[#_#]b : user input                   ? ");
		scanf("%d", &result);

		if (result == 0 || result == 1)
			break;

	} while (1);

	return result;
}

/*-----------------------------------------------------------------------------*/

// user input messeage id
unsigned int input_msg_id(void)
{
	char data[256];
	unsigned int result = 0;

	do {
		memset(data, 0, sizeof(data));
		printf("d[#_#]b : id   (4 byte)                ? x");
		scanf("%s", data);

		if (check_and_convert(data, 8, NULL, NULL, &result) == 0)
			break;

	} while (1);

	return result;
}

/*-----------------------------------------------------------------------------*/

// user input message type
unsigned int input_msg_type(void)
{
	char data[256];
	unsigned char result = 0;
	
	do {
		memset(data, 0, sizeof(data));
		printf("d[#_#]b : message type list(value can combine)\n");
		printf("\r 0x00->STANDARD, 0x01->EXTENDED, 0x02->RTR, 0x04->FD_BRS\n");
		printf("d[#_#]b : msg type (1 byte)                ? x");
		scanf("%s", data);

		if (check_and_convert(data, 2, &result, NULL, NULL) == 0)
			break;

	} while (1);

	return (unsigned int)result;
}

/*-----------------------------------------------------------------------------*/

// user input messeage length
unsigned int input_msg_len_n()
{
	unsigned int result;

	do {
		printf("d[#_#]b : data len                  ? ");
		scanf("%d", &result);

		if (result >=0)
			break;

	} while (1);

	return result;	
}

/*-----------------------------------------------------------------------------*/

// generate random data or not
int input_msg_data_random_enable(void)
{
	int result;

	do {
		printf("d[#_#]b : 0->no random data , 1->random data\n");
		scanf("%d", &result);

		if (result == RANDOM_DATA_DISABLE || result == RANDOM_DATA_ENABLE)
			break;

	} while (1);

	return result;
}

/*-----------------------------------------------------------------------------*/

// user input messeage data
unsigned char input_msg_data(void)
{
	char data[256];
	unsigned char result = 0;

	do {
		memset(data, 0, sizeof(data));
		printf("d[#_#]b : data (1 byte)                ? x");
		scanf("%s", data);

		if (check_and_convert(data, 2, &result, NULL, NULL) == 0)
			break;

	} while (1);

	return result;
}

/*-----------------------------------------------------------------------------*/

// user input can net device name
int input_can_net_device_name(char* name)
{
	printf("Please input CAN net device name(canxxx) ? ");
	memset(name, 0x00, sizeof(char) * BUFF_LEN);
	scanf("%s", name);
	if (strlen(name) <= 0 || strlen(name) > BUFF_LEN) { 
		printf("#ERROR: The length of CAN net device name is invalid\n");
		return -1;
	}

	return 0;
}

/*-----------------------------------------------------------------------------*/

// user input id filter amount
int input_id_filter_amount(void)
{
	int result;

	do {
		printf("d[#_#]b : range from 1 ~ 32\n");
		printf("d[#_#]b : id filter amount#           ? ");
		scanf("%d", &result);

		if (result > 0 && result <= MAXIMUM_IDFILTER_AMOUNT)
			break;

	} while (1);

	return result;
}

/*-----------------------------------------------------------------------------*/

// user input id filter frame type
int input_id_filter_frame_type(void)
{
	int result;

	do {
		printf("d[#_#]b : 0->standard, 1->extended\n");
		printf("d[#_#]b : id filter frame type#           ? ");
		scanf("%d", &result);

		if (result == 0 || result == 1)
			break;

	} while (1);

	return result;
}
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
 J1939 utils input
*****************************************************************************************/

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

// user input J1939 messeage data
unsigned char input_J1939_msg_data(int index)
{
	char data[256];
	unsigned char result = 0;

	do {
		memset(data, 0, sizeof(data));
		printf("d[#_#]b : data (byte %d)                ? x", index);
		scanf("%s", data);

		if (check_and_convert(data, 2, &result, NULL, NULL) == 0)
			break;

	} while (1);

	return result;
}

/*-----------------------------------------------------------------------------*/

// user input J1939 Address
unsigned int input_J1939_address(int addr_type)
{
	char addr[256];
	unsigned char result = 0;

	do {
		if(addr_type == J1939_SA){
			printf("Please input J1939 source address(note: 0xFF = J1939_NO_ADDR, 0xFE = J1939_IDLE_ADDR)):");
			memset(addr, 0, sizeof(addr));
			printf("d[#_#]b : SA (1 byte)                ? x");
			scanf("%s", addr);
		}
		else{
			printf("Please input J1939 destination address(note: 0xFF = J1939_NO_ADDR, 0xFE = J1939_IDLE_ADDR)):");
			memset(addr, 0, sizeof(addr));
			printf("d[#_#]b : DA (1 byte)                ? x");
			scanf("%s", addr);
		}

		if (check_and_convert(addr, 2, &result, NULL, NULL) == 0)
			break;

	} while (1);

	return result;
}

/*-----------------------------------------------------------------------------*/

// user input J1939 PGN
unsigned int input_J1939_pgn()
{
	unsigned int inp_pgn = 0;
	int input_check;

	while(1)
	{
		printf("Please input J1939 PGN:");
		printf("d[#_#]b : PGN   (range:0~3FFFF)                ? x");
		input_check = scanf("%x", &inp_pgn);

		if(input_check != 1)
		{
			printf("Invalid input\n");
			continue;
		}

		if(inp_pgn > 0x3ffff)
		{
			printf("Invalid input : Out of the J1939 PGN Range");
			continue;
		}
		
		break;
	}

	return inp_pgn;
}

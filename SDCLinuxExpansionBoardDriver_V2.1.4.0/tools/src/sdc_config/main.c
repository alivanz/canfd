// SPDX-License-Identifier: MIT
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

#include "precomp.h"

#define VERSION					"2.1.2.0"

struct xlist GB_board_list;

static int _usage(char *prog)
{
	printf("Usage: %s [options] [args]\n\n", prog);
	printf("Options:\n");
	printf("-d                  Display all boards info\n");
	printf("-D [ID]             Display board detail info\n");
	printf("-S [device]         Serial port mode setting (-S help')\n");
	printf("-O [ID] [q] [value] Store DIO output initial value ('-O help')\n");
	printf("-U [ID] [file]      Update firmware ('-U help')\n");
	printf("-V                  Show driver version\n");
	printf("-v                  Show program version\n");

	return -1;
}

int main(int argc, char **argv)
{
	int opt;
	int ret = 0;

	if (argc < 2)
		return _usage(argv[0]);

	if (strlen(argv[1]) != 2)
		return _usage(argv[0]);

	board_list_init(&GB_board_list);

	while ((opt = getopt(argc, argv, "dDShOUVv")) != EOF) {
		switch (opt) {
		case 'd':
			if (argc != 2)
				ret = _usage(argv[0]);
			else
				ret = board_display_all();
			break;
		case 'D':
			if (argc != 3)
				ret = _usage(argv[0]);
			else
				ret = board_display_detail(argv[2]);
			break;
		case 'S':
			if (geteuid() != 0) {
				printf("Please run this as root\n");
				ret = -EACCES;
				break;
			}
			if ((argc == 3) && (strcmp(argv[2], "help") == 0))
				ret = tty_usage(argv[0]);
			else if ((argc == 4) && (strcmp(argv[3], "get") == 0))
				ret = tty_get_rs485_config(argv[2]);
			else if ((argc == 5) && (strcmp(argv[3], "set") == 0))
				ret = tty_set_rs485_config(argv[0], argv[2],
														argv[4], NULL);
			else if ((argc == 6) && (strcmp(argv[3], "set") == 0))
				ret = tty_set_rs485_config(argv[0], argv[2],
														argv[4], argv[5]);
			else
				ret = tty_usage(argv[0]);
			break;
		case 'O':
			if ((argc == 3) && (strcmp(argv[2], "help") == 0))
				ret = config_dio_usage(argv[0]);
			else if (argc > 4)
				if (strcmp(argv[2], "q") == 0)
					ret = config_dio_store(1, argv[3], argc - 4, &argv[4]);
				else
					ret = config_dio_store(0, argv[2], argc - 3, &argv[3]);
			else
				ret = config_dio_usage(argv[0]);
			break;
		case 'U':
			if ((argc == 3) && (strcmp(argv[2], "help") == 0))
				ret = config_firmware_usage(argv[0]);
			else if (argc == 4)
				ret = config_firmware_update(argv[2], argv[3]);
			else
				ret = config_firmware_usage(argv[0]);
			break;
		case 'V':
			ret = driver_version();
			break;
		case 'v':
			printf("Program version=%s\n", VERSION);
			break;
		case 'h':
			_usage(argv[0]);
			break;
		default:
			ret = _usage(argv[0]);
			break;
		}
	}

	board_list_free(&GB_board_list);

	return ret;
}

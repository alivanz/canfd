// SPDX-License-Identifier: MIT
/*
 * SDC UART config program
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/serial.h>

#include "../lib/sdcboardlib.h"

#define VERSION					"2.1.2.0"

struct xlist GB_board_list;

struct uart_cfg {
	int line;
	int flags;
};

static int _get_x(char *buff, char *x, int *x_val, int to_hex)
{
	char ptr[32], *p1, *p2;

	if (!buff)
		return -1;

	p1 = strstr(buff, x);
	if (!p1)
		return -1;

	p1 = strstr(p1, "=");
	if (!p1)
		return -1;
	p1 += 1;

	p2 = strstr(p1, ";");
	if (!p2)
		return -1;

	memset(ptr, 0, sizeof(ptr));
	memcpy(ptr, p1, p2 - p1);
	if (to_hex)
		*x_val = (int)strtol(ptr, NULL, 16);
	else
		*x_val = atoi(ptr);

	return 0;
}

static int _save_config_to_file(void)
{
	struct serial_rs485 rs485;
	struct sdc_board *board;
	struct uart_cfg uart[256] = {0};
	struct xlist *entry;
	FILE *fp_cfg = NULL;
	char buff[512];
	int i, fd, amount = 0;
	int ret;

	entry = GB_board_list.next;
	while (entry && entry != &GB_board_list) {
		board = SDC_BOARD_PTR(entry);
		if (board && !board->error) {
			for (i = 0; i < board->nr_controller; i++) {
				if (board->chls[i].type != SDC_DEV_TYPE_UART)
					continue;
				if (board->chls[i].line == -1)
					continue;

				fd = open(board->chls[i].device, O_RDWR);
				if (fd < 0) {
					printf("Failed to open %s, errno %d\n",
						board->chls[i].device, errno);
					continue;
				}

				if (ioctl(fd, TIOCGRS485, &rs485) < 0) {
					printf("Failed to get rs485 %s, errno %d\n",
						board->chls[i].device, errno);
					close(fd);
					continue;
				}

				close(fd);

				uart[amount].line = board->chls[i].line;
				uart[amount].flags = rs485.flags;
				amount++;
			}
		}

		entry = entry->next;
		board = NULL;
	}

	if (amount == 0) {
		printf("No SDC UART found in driver\n");
		return 0;
	}

	ret = mkdir(SDC_UART_CFG_DIR, 0700);
	if (ret != 0 && errno != EEXIST) {
		printf("Failed to create %s, errno %d\n", SDC_UART_CFG_DIR, errno);
		return -errno;
	}

	fp_cfg = fopen(SDC_UART_CFG_FILE, "wb");
	if (!fp_cfg) {
		printf("Failed to open %s, errno %d\n", SDC_UART_CFG_FILE, errno);
		return -errno;
	}

	for (i = 0; i < amount; i++) {
		snprintf(buff, sizeof(buff), "line=%d;flags=%08X;\n",
			uart[i].line, uart[i].flags);
		fputs(buff, fp_cfg);
		printf("SAVE %s", buff);
	}
	fclose(fp_cfg);

	return 0;
}

static int _restore_config_from_file(void)
{
	struct uart_cfg uart[256] = {0};
	struct serial_rs485 rs485;
	FILE *fp_cfg = NULL;
	char buff[512];
	int i, fd, line, flags, amount = 0;

	fp_cfg = fopen(SDC_UART_CFG_FILE, "r");
	if (!fp_cfg) {
		printf("Failed to open %s, errno %d\n", SDC_UART_CFG_FILE, errno);
		return -errno;
	}

	while (fgets(buff, sizeof(buff), fp_cfg) != NULL) {
		if (_get_x(buff, "line", &line, 0) < 0)
			continue;
		if (line < 0 || line > 255)
			continue;
		if (_get_x(buff, "flags", &flags, 1) < 0)
			continue;

		uart[amount].line = line;
		uart[amount].flags = flags;
		amount++;
	}
	fclose(fp_cfg);

	if (amount == 0) {
		printf("No SDC UART found in file\n");
		return 0;
	}

	for (i = 0; i < amount; i++) {
		snprintf(buff, sizeof(buff), "/dev/%s%d",
			SDC_UART_DEV_NAME, uart[i].line);
		fd = open(buff, O_RDWR);
		if (fd < 0) {
			printf("Failed to open %s, errno %d\n", buff, errno);
			continue;
		}

		if (ioctl(fd, TIOCGRS485, &rs485) < 0) {
			close(fd);
			printf("Failed to get rs485 %s, errno %d\n", buff, errno);
			continue;
		}

		rs485.flags = uart[i].flags;
		if (ioctl(fd, TIOCSRS485, &rs485) < 0)
			printf("Failed to set rs485 %s, errno %d\n", buff, errno);

		close(fd);

		snprintf(buff, sizeof(buff), "line=%d;flags=%08X;\n",
			uart[i].line, uart[i].flags);
		printf("RESTORE %s", buff);
	}

	return 0;
}

static int _usage(char *prog)
{
	printf("Usage: %s [options]\n\n", prog);
	printf("Options:\n");
	printf("-s           Save config to file\n");
	printf("-r           Restore config from file\n");
	printf("-v           Show program version\n");

	return -1;
}

int main(int argc, char **argv)
{
	int opt;
	int ret = 0;

	if (argc != 2)
		return _usage(argv[0]);

	if (strlen(argv[1]) != 2)
		return _usage(argv[0]);

	board_list_init(&GB_board_list);

	while ((opt = getopt(argc, argv, "srhv")) != EOF) {
		switch (opt) {
		case 's':
			if (geteuid() != 0) {
				printf("Please run this as root\n");
				ret = -EACCES;
				break;
			}
			ret = _save_config_to_file();
			break;
		case 'r':
			if (geteuid() != 0) {
				printf("Please run this as root\n");
				ret = -EACCES;
				break;
			}
			ret = _restore_config_from_file();
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

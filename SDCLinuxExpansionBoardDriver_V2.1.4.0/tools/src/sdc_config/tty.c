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

static int get_x(char *buff, char *x, int *x_val, int to_hex)
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

static int get_line(char *buff, int *line)
{
	char ptr[32], *p1, *p2;

	if (!buff)
		return -1;

	p1 = strstr(buff, SDC_UART_DEV_NAME);
	if (!p1)
		return -1;

	p1 += strlen(SDC_UART_DEV_NAME);
	p2 = buff + strlen(buff);
	if (!p2)
		return -1;

	memset(ptr, 0, sizeof(ptr));
	memcpy(ptr, p1, p2 - p1);
	*line = atoi(ptr);

	return 0;
}

static void replace_RS485_config_in_file(int cur_line, int cur_flags)
{
	FILE *fp_temp = NULL, *fp_cfg = NULL;
	char buff[512];
	int line, flags, need_update = 0;

	fp_cfg = fopen(SDC_UART_CFG_FILE, "r");
	if (!fp_cfg)
		goto out;

	fp_temp = fopen(SDC_UART_TEMP_FILE, "wb");
	if (!fp_temp)
		goto out;

	while (fgets(buff, sizeof(buff), fp_cfg) != NULL) {
		if (get_x(buff, "line", &line, 0) < 0)
			continue;
		if (get_x(buff, "flags", &flags, 1) < 0)
			continue;

		if (line == cur_line && flags != cur_flags) {
			snprintf(buff, sizeof(buff), "line=%d;flags=%08X;\n",
				cur_line, cur_flags);
			fputs(buff, fp_temp);
			need_update = 1;
		} else {
			fputs(buff, fp_temp);
		}
	}
	fclose(fp_temp);
	fp_temp = NULL;
	fclose(fp_cfg);
	fp_cfg = NULL;

	if (need_update == 1) {
		fp_cfg = fopen(SDC_UART_CFG_FILE, "wb");
		if (!fp_cfg)
			goto out;

		fp_temp = fopen(SDC_UART_TEMP_FILE, "r");
		if (!fp_temp)
			goto out;

		while (fgets(buff, sizeof(buff), fp_temp) != NULL)
			fputs(buff, fp_cfg);

		fclose(fp_temp);
		fp_temp = NULL;
		fclose(fp_cfg);
		fp_cfg = NULL;
	}

out:
	if (fp_temp)
		fclose(fp_temp);
	if (fp_cfg)
		fclose(fp_cfg);
}

int tty_get_rs485_config(char *parm1)
{
	struct serial_rs485 rs485;
	char buff[32];
	int fd;
	int ret;

	if (strlen(parm1) > 16) {
		printf("Invalid device string\n");
		return -EINVAL;
	}

	snprintf(buff, sizeof(buff), "/dev/%s", parm1);
	fd = open(buff, O_RDWR);
	if (fd < 0) {
		printf("Open %s failure, errno %d\n", parm1, errno);
		return -errno;
	}

	if (ioctl(fd, TIOCGRS485, &rs485) == 0) {
		printf("%s config=", parm1);
		if (!(rs485.flags & SER_RS485_ENABLED)) {
			printf("rs232\n");
		} else {
			if (rs485.flags & SER_RS485_RX_DURING_TX)
				printf("rs422 ");
			else
				printf("rs485 ");

			if (rs485.flags & SER_RS485_TERMINATE_BUS)
				printf("term\n");
			else
				printf("\n");
		}

		ret = 0;

	} else {
		printf("Get %s config failure, errno %d\n", parm1, errno);
		ret = -errno;
	}
	close(fd);

	return ret;
}

int tty_set_rs485_config(char *prog, char *parm1, char *parm2, char *parm3)
{
	struct serial_rs485 rs485;
	char buff[32];
	int fd, flags, line;
	int ret;

	if (strlen(parm1) > 16) {
		printf("Invalid device string\n");
		ret = -EINVAL;
		goto out;
	}

	if (strstr(parm1, SDC_UART_DEV_NAME) == NULL) {
		printf("Invalid device string\n");
		ret = -EINVAL;
		goto out;
	}

	if (get_line(parm1, &line) < 0) {
		printf("Invalid device string\n");
		ret = -EINVAL;
		goto out;
	}

	if (strcmp(parm2, "rs232") == 0) {
		flags = 0;
	} else if (strcmp(parm2, "rs422") == 0) {
		flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND |
			SER_RS485_RX_DURING_TX;
	} else if (strcmp(parm2, "rs485") == 0) {
		flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND;
	} else {
		ret = tty_usage(prog);
		goto out;
	}

	if (parm3) {
		if (strcmp(parm3, "term") == 0) {
			flags |= SER_RS485_TERMINATE_BUS;
		} else {
			ret = tty_usage(prog);
			goto out;
		}
	}

	snprintf(buff, sizeof(buff), "/dev/%s", parm1);
	fd = open(buff, O_RDWR);
	if (fd < 0) {
		printf("Open %s failure, errno %d\n", parm1, errno);
		ret = -errno;
		goto out;
	}

	if (ioctl(fd, TIOCGRS485, &rs485) < 0) {
		printf("Set %s config failure, errno %d\n",	parm1, errno);
		ret = -errno;
		goto out_close;
	}

	rs485.flags = flags;
	if (ioctl(fd, TIOCSRS485, &rs485) < 0) {
		printf("Set %s config failure, errno %d\n", parm1, errno);
		ret = -errno;
		goto out_close;
	}

	replace_RS485_config_in_file(line, flags);

	printf("Set %s config success\n", parm1);

	ret = 0;

out_close:
	close(fd);
out:
	return ret;
}

int tty_usage(char *prog)
{
	printf("Usage: %s -S [device] get|set [rs232|rs422|rs485] [term]\n\n",
		prog);
	printf("Example:\n");
	printf("  Get mode (\"-S %s4 get\")\n", SDC_UART_DEV_NAME);
	printf("  Set mode (\"-S %s4 set rs422 term\")\n", SDC_UART_DEV_NAME);
	printf("  Set mode (\"-S %s5 set rs485\")\n", SDC_UART_DEV_NAME);
	printf("  term: bus terminate, for rs422,rs485\n");

	return -1;
}

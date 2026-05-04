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

static int _check_and_convert(char *data, int len, __u8 *u8_ptr,
								__u16 *u16_ptr, __u32 *u32_ptr)
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
				low_b = data[i + 1] > '9' ?
					data[i + 1] - 'a' + 10 :
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
				low_b = data[i + 1] > '9' ?
					data[i + 1] - 'a' + 10 :
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
				low_b = data[i + 1] > '9' ?
					data[i + 1] - 'a' + 10 :
					data[i + 1] - '0';
				u32_d |= ((hi_b << 4) | low_b) << (24 - (i * 4));
			}
			*u32_ptr = u32_d;
		}
		break;
	}

	return 0;
}

int config_dio_store(int quiet, char *parm1, int nr_reg, char **regs)
{
	struct config_dio set, get;
	struct sdc_board *match;
	struct sdc_chl *chl_config, *chl_dio;
	struct xlist *entry;
	__u32 nr_dw, *data;
	int i, j, id, fd;
	int ret;

	for (i = 0; i < strlen(parm1); i++) {
		if (parm1[i] < '0' || parm1[i] > '9') {
			if (!quiet)
				printf("Invalid ID string\n");
			ret = -EINVAL;
			goto out;
		}
	}

	if (xlist_empty(&GB_board_list)) {
		if (!quiet)
			printf("No SDC board in list\n");
		ret = -ENODEV;
		goto out;
	}

	id = atoi(parm1);

	match = NULL;
	entry = GB_board_list.next;
	while (entry && entry != &GB_board_list) {
		match = SDC_BOARD_PTR(entry);
		if (match && match->id == id)
			break;
		entry = entry->next;
		match = NULL;
	}
	if (!match) {
		if (!quiet)
			printf("ID%d not in list\n", id);
		ret = -ENODEV;
		goto out;
	}

	chl_config = NULL;
	for (i = 0; i < match->nr_controller; i++) {
		if (match->chls[i].type == SDC_DEV_TYPE_CONFIG) {
			chl_config = &match->chls[i];
			break;
		}
	}
	if (!chl_config) {
		if (!quiet)
			printf("ID%d no CONFIG channel\n", id);
		ret = -ENODEV;
		goto out;
	}

	chl_dio = NULL;
	for (i = 0; i < match->nr_controller; i++) {
		if (match->chls[i].type == SDC_DEV_TYPE_DIO) {
			chl_dio = &match->chls[i];
			break;
		}
	}
	if (!chl_dio) {
		if (!quiet)
			printf("ID%d no DIO channel\n", id);
		ret = -ENODEV;
		goto out;
	}

	if (nr_reg != (chl_dio->dio.nr_bank + 2)) {
		if (!quiet)
			printf("ID%d DIO number of bank %d\n", id, chl_dio->dio.nr_bank);
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < nr_reg; i++) {
		if (strlen(regs[i]) != 8) {
			if (!quiet)
				printf("Invalid v%d length %d\n",
					i+1, (unsigned int)strlen(regs[i]));
			ret = -EINVAL;
			goto out;
		}
		for (j = 0; j < strlen(regs[i]); j++) {
			if ((regs[i][j] < '0' || regs[i][j] > '9') &&
				(regs[i][j] < 'a' || regs[i][j] > 'f') &&
				(regs[i][j] < 'A' || regs[i][j] > 'F')) {
				if (!quiet)
					printf("Invalid v%d data char \'%c\'\n", i+1, regs[i][j]);
				ret = -EINVAL;
				goto out;
			}
		}
	}

	nr_dw = nr_reg;
	data = malloc(sizeof(__u32) * nr_dw);
	if (!data) {
		if (!quiet)
			printf("Malloc failure\n");
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < nr_dw; i++) {
		if (_check_and_convert(regs[i], 8, NULL, NULL, &data[i]) < 0) {
			if (!quiet)
				printf("Convert v%d failure\n", i+1);
			ret = -EINVAL;
			goto out_free;
		}
	}

	fd = open(chl_config->device, O_RDWR);
	if (fd < 0) {
		if (!quiet)
			printf("Open %s failure, errno %d\n", chl_config->device, errno);
		ret = -errno;
		goto out_free;
	}

	/* erase */
	if (ioctl(fd, CONFIG_DIO_ERASE, NULL) < 0) {
		if (!quiet)
			printf("Erase DIO output initial value failure, errno %d\n",
				errno);
		ret = -errno;
		goto out_close;
	}

	/* set */
	memset(&set, 0, sizeof(set));
	set.nr_bank = nr_reg - 2;
	set.direction = data[0];
	set.output_initial = data[1];
	for (i = 0; i < set.nr_bank; i++)
		set.bank[i] = data[2 + i];

	if (ioctl(fd, CONFIG_DIO_SET, &set) < 0) {
		if (!quiet)
			printf("Set DIO output initial value failure, errno %d\n", errno);
		ret = -errno;
		goto out_close;
	}

	/* get */
	memset(&get, 0, sizeof(get));
	get.nr_bank = nr_reg - 2;
	if (ioctl(fd, CONFIG_DIO_GET, &get) < 0) {
		if (!quiet)
			printf("Get DIO output initial value failure, errno %d\n", errno);
		ret = -errno;
		goto out_close;
	}

	/* confirm */
	if (memcmp(&set, &get, sizeof(struct config_dio)) != 0) {
		if (!quiet)
			printf("Confirm DIO output initial value failure\n");
		ret = -EIO;
		goto out_close;
	}

	if (!quiet)
		printf("Store DIO output initial value ok\n");

	ret = 0;

out_close:
	close(fd);
out_free:
	if (data)
		free(data);
out:
	return ret;
}

int config_dio_usage(char *prog)
{
	printf("Usage: %s -O [q] [ID] [v1] [v2] [v3] [v4] ...\n\n", prog);
	printf("Example:\n");
	printf("  \"-O 1 000000f0 000000a0 00000000 00000001\"\n");
	printf("  \"-O q 1 000000f0 000000a0 00000000 00000001\"\n");
	printf("  q: quiet\n");
	printf("  v1: direction register\n");
	printf("  v2: output initial register\n");
	printf("  v3: bank 0 control register\n");
	printf("  v4: bank 1 control register\n");

	return -1;
}

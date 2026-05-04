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

static unsigned char start_str_1[8] = {
	'S', 'U', 'N', 'I', 'X', 'Q', 'L', 0
};

static unsigned char start_str_2[8] = {
	'S', 'U', 'N', 'I', 'X', 'Q', 'N', 0
};

static void _header_convert(struct firmware_h *h, unsigned char *fcontent)
{
	int index;

	memset(h, 0, sizeof(struct firmware_h));
	index = 0;
	memcpy(h->h_start_str, fcontent, 7);
	index += 7;
	h->h_version = *(fcontent + index++);
	memcpy(h->model_name, fcontent + index, 20);
	index += 20;
	h->major_version = *(fcontent + index++) << 8;
	h->major_version |= *(fcontent + index++);
	h->minor_version = *(fcontent + index++) << 8;
	h->minor_version |= *(fcontent + index++);
	h->control_type = *(fcontent + index++) << 8;
	h->control_type |= *(fcontent + index++);
	memcpy(h->date, fcontent + index, 6);
	index += 6;
	h->device_type_brand = *(fcontent + index++);
	h->device_type_model = *(fcontent + index++);
	h->firmware_data_size = *(fcontent + index++) << 24;
	h->firmware_data_size |= *(fcontent + index++) << 16;
	h->firmware_data_size |= *(fcontent + index++) << 8;
	h->firmware_data_size |= *(fcontent + index++);
	memcpy(h->firmware_md5, fcontent + index, 16);
	index += 16;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static int _header_check(struct firmware_h *h, struct sdc_board *board,
								struct sdc_chl *chl, unsigned char *fcontent)
{
	int check_ok;

	if ((memcmp(h->h_start_str, start_str_1, 7) != 0) &&
		(memcmp(h->h_start_str, start_str_2, 7) != 0)) {
		printf("Check header failure, start_string %s\n", h->h_start_str);
		return -EINVAL;
	}

	if (!(h->control_type & 1)) {
		if (memcmp(board->model_name, h->model_name, 16) != 0) {
			printf("Check header failure, model_name %s\n", h->model_name);
			return -EINVAL;
		}
	}

	if ((chl->config.brand != h->device_type_brand) ||
		(chl->config.model != h->device_type_model)) {
		printf("Check header failure, device_type B:x%02X M:x%02X\n",
			h->device_type_brand, h->device_type_model);
		return -EINVAL;
	}

	check_ok = 0;
	switch (chl->config.brand) {
	case 0x00:
		switch (chl->config.model) {
		case 0x00:
			if (h->firmware_data_size == 0x60000)
				check_ok = 1;
			break;
		}
		break;
	case 0x01:
		switch (chl->config.model) {
		case 0x00:
			if (h->firmware_data_size == 0x130000)
				check_ok = 1;
			break;
		}
		break;
	}
	if (!check_ok) {
		printf("Check header failure, bitstream_size x%08X\n",
			h->firmware_data_size);
		return -EINVAL;
	}

	return 0;
}
#pragma GCC diagnostic pop

static int _firmware_erase(struct firmware_h *h, int fd)
{
	struct config_bitstream b;
	int index, ptotal_len, p10_len, p;

	p = 0;
	p10_len = 0;
	ptotal_len = 0;
	for (index = 0; index < h->firmware_data_size; index += 0x1000) {
		memset(&b, 0, sizeof(b));
		b.addr = index;
		if (ioctl(fd, CONFIG_BITSTREAM_ERASE, &b) < 0)
			break;

		ptotal_len += 0x1000;
		p10_len += 0x1000;

		if (p10_len >= (h->firmware_data_size / 10)) {
			p += 10;
			p10_len = 0;
			printf("Erase %d percent\n", p);
		}
		if (ptotal_len == h->firmware_data_size) {
			p = 100;
			printf("Erase ok\n\n");
			break;
		}
	}

	return p == 100 ? 0 : -EIO;
}

static int _firmware_set(struct firmware_h *h, int fd, unsigned char *fdata)
{
	struct config_bitstream b;
	__u32 data_dw[CONFIG_NVM_MAX], data_len_dw;
	int index, ptotal_len, p10_len, p, data_len;

	p = 0;
	p10_len = 0;
	ptotal_len = 0;
	index = 0;
	data_len = 0;
	data_len_dw = 0;
	while (index < h->firmware_data_size) {
		memset(data_dw, 0, sizeof(data_dw));
		if ((h->firmware_data_size - index) >= (CONFIG_NVM_MAX * 4)) {
			data_len = (CONFIG_NVM_MAX * 4);
			data_len_dw = data_len / 4;

		} else {
			data_len = h->firmware_data_size - index;
			if ((data_len % 4) == 0)
				data_len_dw = data_len / 4;
			else
				data_len_dw = (data_len / 4) + 1;
			if (data_len_dw > CONFIG_NVM_MAX)
				data_len_dw = CONFIG_NVM_MAX;
		}

		memcpy(data_dw, fdata + index, sizeof(__u32) * data_len_dw);

		memset(&b, 0, sizeof(b));
		b.addr = index;
		b.len = data_len_dw;
		memcpy(b.data, data_dw, sizeof(__u32) * data_len_dw);
		if (ioctl(fd, CONFIG_BITSTREAM_SET, &b) < 0)
			break;

		ptotal_len += data_len;
		p10_len += data_len;
		index += data_len;

		if (p10_len >= (h->firmware_data_size / 10)) {
			p += 10;
			p10_len = 0;
			printf("Update %d percent\n", p);
		}
		if (ptotal_len == h->firmware_data_size) {
			p = 100;
			printf("Update ok\n\n");
			break;
		}
	}

	return p == 100 ? 0 : -EIO;
}

static int _firmware_get_and_confirm(struct firmware_h *h, int fd,
								unsigned char *fdata)
{
	struct config_bitstream b;
	__u32 data_dw[CONFIG_NVM_MAX], data_len_dw;
	int index, ptotal_len, p10_len, p, data_len;
	int i, confirm_fail = 0;

	p = 0;
	p10_len = 0;
	ptotal_len = 0;
	index = 0;
	data_len = 0;
	data_len_dw = 0;
	while (index < h->firmware_data_size) {
		memset(data_dw, 0, sizeof(data_dw));
		if ((h->firmware_data_size - index) >= (CONFIG_NVM_MAX * 4)) {
			data_len = (CONFIG_NVM_MAX * 4);
			data_len_dw = data_len / 4;

		} else {
			data_len = h->firmware_data_size - index;
			if ((data_len % 4) == 0)
				data_len_dw = data_len / 4;
			else
				data_len_dw = (data_len / 4) + 1;
			if (data_len_dw > CONFIG_NVM_MAX)
				data_len_dw = CONFIG_NVM_MAX;
		}

		memcpy(data_dw, fdata + index, sizeof(__u32) * data_len_dw);

		memset(&b, 0, sizeof(b));
		b.addr = index;
		b.len = data_len_dw;
		if (ioctl(fd, CONFIG_BITSTREAM_GET, &b) < 0)
			break;

		for (i = 0; i < data_len_dw; i++) {
			if (data_dw[i] != b.data[i]) {
				confirm_fail = 1;
				break;
			}
		}
		if (confirm_fail)
			break;

		ptotal_len += data_len;
		p10_len += data_len;
		index += data_len;

		if (p10_len >= (h->firmware_data_size / 10)) {
			p += 10;
			p10_len = 0;
			printf("Confirm %d percent\n", p);
		}
		if (ptotal_len == h->firmware_data_size) {
			p = 100;
			printf("Confirm ok\n\n");
			break;
		}
	}

	return p == 100 ? 0 : -EIO;
}

int config_firmware_update(char *parm1, char *parm2)
{
	struct firmware_h h;
	struct sdc_board *match;
	struct sdc_chl *chl_config;
	struct xlist *entry;
	FILE *fp;
	long flength, rx_total;
	__u8 *fcontent;
	int i, id, fd, fd_up, len;
	int ret;

	for (i = 0; i < strlen(parm1); i++) {
		if (parm1[i] < '0' || parm1[i] > '9') {
			printf("Invalid ID string\n");
			ret = -EINVAL;
			goto out;
		}
	}

	if (xlist_empty(&GB_board_list)) {
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
		printf("ID%d no CONFIG channel\n", id);
		ret = -ENODEV;
		goto out;
	}

	/* get file size */
	fp = fopen(parm2, "rb");
	if (!fp) {
		printf("Open %s failure\n", parm2);
		ret = -ENOENT;
		goto out;
	}
	fseek(fp, 0, SEEK_END);
	flength = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	fclose(fp);
	if (flength <= 128) {
		printf("File no content\n");
		ret = -EINVAL;
		goto out;
	}

	fcontent = malloc(flength + 256);
	if (!fcontent) {
		printf("Malloc failure\n");
		ret = -ENOMEM;
		goto out;
	}
	memset(fcontent, 0, flength + 256);

	/* get file content */
	fd_up = open(parm2, O_RDONLY);
	if (fd_up < 0) {
		printf("Open %s failure, errno %d\n", parm2, errno);
		ret = -errno;
		goto out_free;
	}
	rx_total = 0;
	while (1) {
		len = read(fd_up, fcontent + rx_total, flength - rx_total);
		if (len <= 0)
			break;
		rx_total += len;
	}
	close(fd_up);
	if (rx_total != flength) {
		printf("Read %s failure\n", parm2);
		ret = -ENOENT;
		goto out_free;
	}

	_header_convert(&h, fcontent);

	if (_header_check(&h, match, chl_config, fcontent) < 0) {
		ret = -EINVAL;
		goto out_free;
	}

	fd = open(chl_config->device, O_RDWR);
	if (fd < 0) {
		printf("Open %s failure, errno %d\n", chl_config->device, errno);
		ret = -errno;
		goto out_free;
	}

	/* erase */
	ret = _firmware_erase(&h, fd);
	if (ret < 0) {
		printf("Erase firmware failure, please try again\n");
		goto out_close;
	}

	/* set */
	ret = _firmware_set(&h, fd, fcontent + 128);
	if (ret < 0) {
		printf("Update firmware failure, please try again\n");
		goto out_close;
	}

	/* get and confirm */
	ret = _firmware_get_and_confirm(&h, fd, fcontent + 128);
	if (ret < 0) {
		printf("Confirm firmware failure, please try again\n");
		goto out_close;
	}

	printf("Update firmware ok\n");
	printf("Please shutdown system and power up\n");

	ret = 0;

out_close:
	close(fd);
out_free:
	if (fcontent)
		free(fcontent);
out:
	return ret;
}

int config_firmware_usage(char *prog)
{
	printf("Usage: %s -U [ID] [file]\n\n", prog);
	printf("Example:\n");
	printf("  \"-U 1 /home/test/SDC4441LL_v1_005.rom\"\n");

	return -1;
}

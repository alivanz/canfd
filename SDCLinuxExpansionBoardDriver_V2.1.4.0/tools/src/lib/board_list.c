// SPDX-License-Identifier: MIT
/*
 * SDC static library
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
#include <dirent.h>
#include <sys/ioctl.h>

#include "sdcboardlib.h"

#define DBG_PROC_INFO			0
#define DBG_BOARD_INFO			0
#define DBG_CONFIG_LINE_PID		0
#define DBG_UART_LINE_PID		0
#define DBG_DIO_LINE_PID		0
#define DBG_SPI_CAN_LINE_PID	0
#define DBG_SDC_CAN_LINE_PID	0
#define DBG_PARPORT_LINE_PID	0

#define PROC_MAX				256

struct proc_basic {
	int pci_bus;
	int irq;
	int index;
	int pid;
};

struct proc_config {
	struct proc_basic b;
	int alive;
	int line;
	char name[16];
};

struct proc_uart {
	struct proc_basic b;
	int alive;
	int line;
	char name[16];
};

struct proc_dio {
	struct proc_basic b;
	int alive;
	int line;
	char name[16];
};

struct proc_spi_device {
	int spi_bus;
	int cs;
	int line;
	char name[16];
};

struct proc_spi {
	struct proc_basic b;
	int alive;
	int nr_device;
	struct proc_spi_device devices[SDC_SPI_DEVICE_MAX];
};

struct proc_can {
	struct proc_basic b;
	int alive;
	int line;
	char name[16];
};

struct proc_parport {
	struct proc_basic b;
	int alive;
	int line;
	char name[16];
};

struct proc_gpio {
	struct proc_basic b;
	int alive;
	int id;
	char label[32];
	char d_name[32];
	int line;
	char name[32];
};

struct proc_info {
	struct proc_config pconfig[PROC_MAX];
	struct proc_uart puart[PROC_MAX];
	struct proc_dio pdio[PROC_MAX];
	struct proc_spi pspi[PROC_MAX];
	struct proc_can pcan[PROC_MAX];
	struct proc_parport pparport[PROC_MAX];
	struct proc_gpio pgpio[PROC_MAX];
};

static int _get_x(char *buff, char *x, int *x_val)
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
	*x_val = atoi(ptr);

	return 0;
}

/* for CONFIG, DIO, UART, PARPORT */
static int _get_line(char *buff, int *num)
{
	char ptr[32], *p1, *p2;

	if (!buff)
		return -1;

	p1 = strstr(buff, "line");
	if (!p1)
		return -1;

	p1 += strlen("line");
	p2 = buff + strlen(buff);
	if (!p2)
		return -1;

	memset(ptr, 0, sizeof(ptr));
	memcpy(ptr, p1, p2 - p1);
	*num = atoi(ptr);

	return 0;
}

/* for SPI CAN */
static int _get_spi_bus(char *buff, int *bus)
{
	char ptr[32], *p1, *p2;

	if (!buff)
		return -1;

	p1 = strstr(buff, "spi_bus");
	if (!p1)
		return -1;

	p1 += strlen("spi_bus");
	p2 = buff + strlen(buff);
	if (!p2)
		return -1;

	memset(ptr, 0, sizeof(ptr));
	memcpy(ptr, p1, p2 - p1);
	*bus = atoi(ptr);

	return 0;
}

/* for SPI CAN */
static int _get_spi_can(char *buff, int *can)
{
	char ptr[32], *p1, *p2;

	if (!buff)
		return -1;

	p1 = strstr(buff, "can");
	if (!p1)
		return -1;

	p1 += strlen("can");
	p2 = buff + strlen(buff);
	if (!p2)
		return -1;

	memset(ptr, 0, sizeof(ptr));
	memcpy(ptr, p1, p2 - p1);
	*can = atoi(ptr);

	return 0;
}

/* for SPI CAN */
static int _get_spi_device(char *buff, int dev_idx, struct proc_spi_device *d)
{
	char tmp[32];

	snprintf(tmp, sizeof(tmp), "spi_bus_%d", dev_idx);
	if (_get_x(buff, tmp, &d->spi_bus) < 0)
		return -1;

	snprintf(tmp, sizeof(tmp), "cs_%d", dev_idx);
	if (_get_x(buff, tmp, &d->cs) < 0)
		return -1;

	return 0;
}

/* for SDC CAN */
static int _get_sdc_can_line(char *buff, int *line)
{
	char ptr[32], *p1, *p2;

	if (!buff)
		return -1;

	p1 = strstr(buff, "can");
	if (!p1)
		return -1;

	p1 += strlen("can");
	p2 = buff + strlen(buff);
	if (!p2)
		return -1;

	memset(ptr, 0, sizeof(ptr));
	memcpy(ptr, p1, p2 - p1);
	*line = atoi(ptr);

	return 0;
}

/* for GPIO */
static int _get_id(char *buff, int *num)
{
	char ptr[32], *p1, *p2;

	if (!buff)
		return -1;

	p1 = strstr(buff, "id");
	if (!p1)
		return -1;

	p1 += strlen("id");
	p2 = buff + strlen(buff);
	if (!p2)
		return -1;

	memset(ptr, 0, sizeof(ptr));
	memcpy(ptr, p1, p2 - p1);
	*num = atoi(ptr);

	return 0;
}

static int _get_sdc_gpio_label(char *buff, char *output, size_t size)
{
	char *p1, *p2;

	if (!buff)
		return -1;

	p1 = strstr(buff, "label");
	if (!p1)
		return -1;

	p1 = strstr(p1, "=");
	if (!p1)
		return -1;

	p1 += 1;
	p2 = strstr(p1, ";");
	if (!p2)
		return -1;

	if ((p2 - p1) >= size)
		return -1;

	memset(output, 0, size);
	memcpy(output, p1, p2 - p1);

	return 0;
}

static int _get_sdc_gpio_line(char *buff, int *num)
{
	char ptr[32], *p1, *p2;

	if (!buff)
		return -1;

	p1 = strstr(buff, "gpiochip");
	if (!p1)
		return -1;

	p1 += strlen("gpiochip");
	p2 = buff + strlen(buff);
	if (!p2)
		return -1;

	memset(ptr, 0, sizeof(ptr));
	memcpy(ptr, p1, p2 - p1);
	*num = atoi(ptr);

	return 0;
}

static int _get_basic(char *buff, struct proc_basic *b)
{
	if (_get_x(buff, "pci_bus", &b->pci_bus) < 0)
		return -1;

	if (_get_x(buff, "irq", &b->irq) < 0)
		return -1;

	if (_get_x(buff, "index", &b->index) < 0)
		return -1;

	if (_get_x(buff, "pid", &b->pid) < 0)
		return -1;

	return 0;
}

static void _proc_init(struct proc_info *pinfo)
{
	struct dirent *ent;
	DIR *dir = NULL, *dir2 = NULL;
	FILE *fp = NULL;
	struct proc_spi_device d;
	struct proc_basic b;
	char *buff = NULL, *temp = NULL;
	int i, line, spi_bus, idx, nr_device, id, match;
	int ret;

	if (!pinfo)
		return;

	memset(pinfo, 0, sizeof(*pinfo));

	buff = malloc(BUFF_LENGTH);
	if (!buff)
		return;

	/* get proc CONFIG */
	idx = 0;
	snprintf(buff, BUFF_LENGTH, "/proc/%s", SDC_CONFIG_DRV_NAME);
	dir = opendir(buff);
	if (dir) {
		while ((ent = readdir(dir)) != NULL) {
			if (strncmp(ent->d_name, "line", strlen("line")) != 0)
				continue;
			if (_get_line(ent->d_name, &line) < 0)
				continue;
			if (line < 0)
				continue;

			snprintf(buff, BUFF_LENGTH, "/proc/%s/%s",
				SDC_CONFIG_DRV_NAME, ent->d_name);
			fp = fopen(buff, "r");
			if (!fp)
				continue;
			memset(buff, 0, BUFF_LENGTH);
			temp = fgets(buff, BUFF_LENGTH, fp);
			fclose(fp);
			if (!temp)
				continue;

			if (_get_basic(buff, &b) < 0)
				continue;
			memcpy(&pinfo->pconfig[idx].b, &b, sizeof(b));

			pinfo->pconfig[idx].line = line;
			if (line < 256)
				snprintf(pinfo->pconfig[idx].name, 16, "%s%d",
					SDC_CONFIG_DEV_NAME, line);
			pinfo->pconfig[idx].alive = 1;

			idx++;
			if (idx >= PROC_MAX)
				break;
		}
		closedir(dir);
	}

	/* get proc UART */
	idx = 0;
	snprintf(buff, BUFF_LENGTH, "/proc/%s", SDC_UART_DRV_NAME);
	dir = opendir(buff);
	if (dir) {
		while ((ent = readdir(dir)) != NULL) {
			if (strncmp(ent->d_name, "line", strlen("line")) != 0)
				continue;
			if (_get_line(ent->d_name, &line) < 0)
				continue;
			if (line < 0)
				continue;

			snprintf(buff, BUFF_LENGTH, "/proc/%s/%s",
				SDC_UART_DRV_NAME, ent->d_name);
			fp = fopen(buff, "r");
			if (!fp)
				continue;
			memset(buff, 0, BUFF_LENGTH);
			temp = fgets(buff, BUFF_LENGTH, fp);
			fclose(fp);
			if (!temp)
				continue;

			if (_get_basic(buff, &b) < 0)
				continue;
			memcpy(&pinfo->puart[idx].b, &b, sizeof(b));

			pinfo->puart[idx].line = line;
			if (line < 256)
				snprintf(pinfo->puart[idx].name, 16, "%s%d",
					SDC_UART_DEV_NAME, line);
			pinfo->puart[idx].alive = 1;

			idx++;
			if (idx >= PROC_MAX)
				break;
		}
		closedir(dir);
	}

	/* get proc DIO */
	idx = 0;
	snprintf(buff, BUFF_LENGTH, "/proc/%s", SDC_DIO_DRV_NAME);
	dir = opendir(buff);
	if (dir) {
		while ((ent = readdir(dir)) != NULL) {
			if (strncmp(ent->d_name, "line", strlen("line")) != 0)
				continue;
			if (_get_line(ent->d_name, &line) < 0)
				continue;
			if (line < 0)
				continue;

			snprintf(buff, BUFF_LENGTH, "/proc/%s/%s",
				SDC_DIO_DRV_NAME, ent->d_name);
			fp = fopen(buff, "r");
			if (!fp)
				continue;
			memset(buff, 0, BUFF_LENGTH);
			temp = fgets(buff, BUFF_LENGTH, fp);
			fclose(fp);
			if (!temp)
				continue;

			if (_get_basic(buff, &b) < 0)
				continue;
			memcpy(&pinfo->pdio[idx].b, &b, sizeof(b));

			pinfo->pdio[idx].line = line;
			if (line < 256)
				snprintf(pinfo->pdio[idx].name, 16, "%s%d",
					SDC_DIO_DEV_NAME, line);
			pinfo->pdio[idx].alive = 1;

			idx++;
			if (idx >= PROC_MAX)
				break;
		}
		closedir(dir);
	}

	/* get proc SPI */
	idx = 0;
	snprintf(buff, BUFF_LENGTH, "/proc/%s", SDC_SPI_DRV_NAME);
	dir = opendir(buff);
	if (dir) {
		while ((ent = readdir(dir)) != NULL) {
			if (strncmp(ent->d_name, "spi_bus", strlen("spi_bus")) != 0)
				continue;
			if (_get_spi_bus(ent->d_name, &spi_bus) < 0)
				continue;
			if (spi_bus < 0)
				continue;

			snprintf(buff, BUFF_LENGTH, "/proc/%s/%s",
				SDC_SPI_DRV_NAME, ent->d_name);
			fp = fopen(buff, "r");
			if (!fp)
				continue;
			memset(buff, 0, BUFF_LENGTH);
			temp = fgets(buff, BUFF_LENGTH, fp);
			fclose(fp);
			if (!temp)
				continue;

			if (_get_basic(buff, &b) < 0)
				continue;
			memcpy(&pinfo->pspi[idx].b, &b, sizeof(b));

			if (_get_x(buff, "nr_device", &nr_device) < 0)
				continue;
			pinfo->pspi[idx].nr_device = nr_device;

			ret = 0;
			for (i = 0; i < nr_device; i++) {
				memset(&d, 0, sizeof(d));
				if (_get_spi_device(buff, i, &d) < 0) {
					ret = -1;
					break;
				}
				memcpy(&pinfo->pspi[idx].devices[i], &d, sizeof(d));
			}
			if (ret < 0)
				continue;

			pinfo->pspi[idx].alive = 1;

			idx++;
			if (idx >= PROC_MAX)
				break;
		}
		closedir(dir);
	}

	/* get SPI CAN device line */
	for (idx = 0; idx < PROC_MAX; idx++) {
		if (pinfo->pspi[idx].alive == 0)
			continue;

		for (i = 0; i < pinfo->pspi[idx].nr_device; i++) {
			snprintf(buff, BUFF_LENGTH,
				"/sys/class/spi_master/spi%d/spi%d.%d/net",
				pinfo->pspi[idx].devices[i].spi_bus,
				pinfo->pspi[idx].devices[i].spi_bus,
				pinfo->pspi[idx].devices[i].cs);

			dir = opendir(buff);
			if (!dir)
				continue;
			while ((ent = readdir(dir)) != NULL) {
				if (strncmp(ent->d_name, "can", strlen("can")) != 0)
					continue;
				ret = _get_spi_can(ent->d_name, &line);
				if (ret < 0)
					continue;
				if (line < 0)
					continue;
				pinfo->pspi[idx].devices[i].line = line;
				if (line < 256)
					snprintf(pinfo->pspi[idx].devices[i].name, 16, "%s%d",
						SDC_CAN_DEV_NAME, line);
				break;
			}
			closedir(dir);
		}
	}

	/* get proc SDC CAN */
	idx = 0;
	snprintf(buff, BUFF_LENGTH, "/proc/%s", SDC_CAN_DRV_NAME);
	dir = opendir(buff);
	if (dir) {
		while ((ent = readdir(dir)) != NULL) {
			if (strncmp(ent->d_name, "can", strlen("can")) != 0)
				continue;
			if (_get_sdc_can_line(ent->d_name, &line) < 0)
				continue;
			if (line < 0)
				continue;

			snprintf(buff, BUFF_LENGTH, "/proc/%s/%s",
				SDC_CAN_DRV_NAME, ent->d_name);
			fp = fopen(buff, "r");
			if (!fp)
				continue;
			memset(buff, 0, BUFF_LENGTH);
			temp = fgets(buff, BUFF_LENGTH, fp);
			fclose(fp);
			if (!temp)
				continue;

			if (_get_basic(buff, &b) < 0)
				continue;
			memcpy(&pinfo->pcan[idx].b, &b, sizeof(b));

			pinfo->pcan[idx].line = line;
			if (line < 256)
				snprintf(pinfo->pcan[idx].name, 16, "%s%d",
					SDC_CAN_DEV_NAME, line);
			pinfo->pcan[idx].alive = 1;

			idx++;
			if (idx >= PROC_MAX)
				break;
		}
		closedir(dir);
	}

	/* get proc PARPORT */
	idx = 0;
	snprintf(buff, BUFF_LENGTH, "/proc/%s", SDC_PARPORT_DRV_NAME);
	dir = opendir(buff);
	if (dir) {
		while ((ent = readdir(dir)) != NULL) {
			if (strncmp(ent->d_name, "line", strlen("line")) != 0)
				continue;
			if (_get_line(ent->d_name, &line) < 0)
				continue;
			if (line < 0)
				continue;

			snprintf(buff, BUFF_LENGTH, "/proc/%s/%s",
				SDC_PARPORT_DRV_NAME, ent->d_name);
			fp = fopen(buff, "r");
			if (!fp)
				continue;
			memset(buff, 0, BUFF_LENGTH);
			temp = fgets(buff, BUFF_LENGTH, fp);
			fclose(fp);
			if (!temp)
				continue;

			if (_get_basic(buff, &b) < 0)
				continue;
			memcpy(&pinfo->pparport[idx].b, &b, sizeof(b));

			pinfo->pparport[idx].line = line;
			if (line < 256)
				snprintf(pinfo->pparport[idx].name, 16, "%s%d",
					SDC_PARPORT_DEV_NAME, line);
			pinfo->pparport[idx].alive = 1;

			idx++;
			if (idx >= PROC_MAX)
				break;
		}
		closedir(dir);
	}

	/* get proc GPIO */
	idx = 0;
	snprintf(buff, BUFF_LENGTH, "/proc/%s", SDC_GPIO_DRV_NAME);
	dir = opendir(buff);
	if (dir) {
		while ((ent = readdir(dir)) != NULL) {
			if (strncmp(ent->d_name, "id", strlen("id")) != 0)
				continue;
			if (_get_id(ent->d_name, &id) < 0)
				continue;
			if (id < 0)
				continue;

			snprintf(buff, BUFF_LENGTH, "/proc/%s/%s",
				SDC_GPIO_DRV_NAME, ent->d_name);
			fp = fopen(buff, "r");
			if (!fp)
				continue;
			memset(buff, 0, BUFF_LENGTH);
			temp = fgets(buff, BUFF_LENGTH, fp);
			fclose(fp);
			if (!temp)
				continue;

			if (_get_basic(buff, &b) < 0)
				continue;
			if (id >= 256)
				continue;

			pinfo->pgpio[idx].id = id;
			memcpy(&pinfo->pgpio[idx].b, &b, sizeof(b));

			if (_get_sdc_gpio_label(buff, pinfo->pgpio[idx].label,
					sizeof(pinfo->pgpio[idx].label)) < 0)
				continue;

			dir2 = opendir("/sys/class/gpio");
			if (!dir2)
				continue;

			match = 0;
			while ((ent = readdir(dir2)) != NULL) {
				if (strncmp(ent->d_name, "gpiochip", strlen("gpiochip")) != 0)
					continue;
				snprintf(buff, BUFF_LENGTH, "/sys/class/gpio/%s/label",
					ent->d_name);
				fp = fopen(buff, "r");
				if (!fp)
					continue;
				memset(buff, 0, BUFF_LENGTH);
				temp = fgets(buff, BUFF_LENGTH, fp);
				fclose(fp);
				if (!temp)
					continue;
				if (strncmp(buff, pinfo->pgpio[idx].label,
					strlen(pinfo->pgpio[idx].label)) != 0)
					continue;
				if (strlen(ent->d_name) >= 32)
					continue;
				strcpy(pinfo->pgpio[idx].d_name, ent->d_name);
				match = 1;
				break;
			}

			closedir(dir2);

			if (!match)
				continue;

			snprintf(buff, BUFF_LENGTH, "/sys/class/gpio/%s/device",
				pinfo->pgpio[idx].d_name);
			dir2 = opendir(buff);
			if (!dir2)
				continue;

			while ((ent = readdir(dir2)) != NULL) {
				if (strncmp(ent->d_name, "gpiochip", strlen("gpiochip")) != 0)
					continue;
				if (_get_sdc_gpio_line(ent->d_name, &line) == 0) {
					pinfo->pgpio[idx].line = line;
					snprintf(pinfo->pgpio[idx].name, 32, "%s%d",
						SDC_GPIO_DEV_NAME, line);
					pinfo->pgpio[idx].alive = 1;
					break;
				}
			}

			closedir(dir2);

			idx++;
			if (idx >= PROC_MAX)
				break;
		}
		closedir(dir);
	}

	if (buff)
		free(buff);
}

static void _board_init(struct sdc_board *board)
{
	const char *src = "unknow";
	int i, j;

	board->pci_bus = -1;
	board->id = -1;
	board->irq = -1;

	for (i = 0; i < SDC_CHL_MAX; i++) {
		board->chls[i].type = 0xff;
		board->chls[i].supported = 0;
		board->chls[i].pid = -1;
		board->chls[i].line = -1;
		strcpy(board->chls[i].name, src);
		strcpy(board->chls[i].device, src);
		for (j = 0; j < SDC_SPI_DEVICE_MAX; j++) {
			board->chls[i].spi.devices[j].can_line = -1;
			strcpy(board->chls[i].spi.devices[j].can_name, src);
			strcpy(board->chls[i].spi.devices[j].can_device, src);
		}
	}
}

static int _board_get_hw_basic(int fd, struct sdc_board *board,
								__u32 *next_offset)
{
	struct config_regs rs;
	struct config_reg r;
	__u32 temp, offset;
	int i, j;

	r.offset = 0;
	if (ioctl(fd, CONFIG_GET_INFO_REG, &r) < 0)
		return ERR_IOCTL_FAILURE;

	board->major_version = r.value & 0x000000ff;
	board->minor_version = (r.value & 0x0000ff00) >> 8;
	board->nr_controller = (r.value & 0x00ff0000) >> 16;
	board->dib_total_length = (r.value & 0xff000000) >> 24;

	rs.offset = 1;
	rs.count = board->dib_total_length - 1;
	if (ioctl(fd, CONFIG_GET_INFO_REGS, &rs) < 0)
		return ERR_IOCTL_FAILURE;

	board->next_ptr = rs.value[0] & 0x0000ffff;
	offset = board->next_ptr / 4;
	*next_offset = offset;

	j = 0;
	memset(board->model_name, 0, 17);
	for (i = 0; i < 4; i++) {
		temp = rs.value[i + 1];
		board->model_name[j++] = temp & 0x000000ff;
		board->model_name[j++] = (temp & 0x0000ff00) >> 8;
		board->model_name[j++] = (temp & 0x00ff0000) >> 16;
		board->model_name[j++] = (temp & 0xff000000) >> 24;
	}
	board->model_name[16] = '\0';

	return 0;
}

static void _board_get_hw_config(struct config_regs *rs, struct sdc_chl *chl)
{
	// rs->value[0] -> DW 2
	// rs->value[1] -> DW 3
	switch (chl->version) {
	case 0x00:
		chl->config.mem_offset = rs->value[1];
		chl->config.mem_size = rs->value[2];
		chl->config.model = rs->value[3] & 0x000000ff;
		chl->config.brand = (rs->value[3] & 0x0000ff00) >> 8;
		chl->supported = 1;
		break;
	default:
		break;
	}
}

static void _board_get_hw_uart(struct config_regs *rs, struct sdc_chl *chl)
{
	// rs->value[0] -> DW 2
	// rs->value[1] -> DW 3
	switch (chl->version) {
	case 0x00:
		chl->uart.io_offset = (rs->value[1] & 0x00ffffff);
		chl->uart.io_size = (rs->value[1] & 0xff000000) >> 24;
		chl->uart.mem_offset = rs->value[2];
		chl->uart.mem_size = rs->value[3];
		chl->uart.tx_fifo_size = rs->value[4] & 0x0000ffff;
		chl->uart.rx_fifo_size = (rs->value[4] & 0xffff0000) >> 16;
		chl->uart.clk_sig = rs->value[5] & 0x00ffffff;
		chl->uart.clk_exp = (rs->value[5] & 0xffffff00) >> 24;
		chl->uart.capability = rs->value[6];
		chl->supported = 1;
		break;
	default:
		break;
	}
}

static void _board_get_hw_dio(struct config_regs *rs, struct sdc_chl *chl)
{
	struct sdc_dio_bank *bank;
	__u32 io_shift_bits;
	__u32 io_mask;
	int i, j;

	// rs->value[0] -> DW 2
	// rs->value[1] -> DW 3
	switch (chl->version) {
	case 0x00:
		chl->dio.mem_offset = rs->value[1];
		chl->dio.mem_size = rs->value[2];
		chl->dio.nr_bank = rs->value[3] & 0x000000ff;
		chl->dio.capability = (rs->value[3] & 0x00000300) >> 8;

		if (chl->dio.nr_bank > SDC_DIO_BANK_MAX)
			return;

		chl->dio.di_sampling_freq = 0;
		chl->dio.di_filter_lower_bound = 0;
		for (i = 0; i < chl->dio.nr_bank; i++) {
			chl->dio.banks[i].nr_io = rs->value[4 + i] & 0x000000ff;
			chl->dio.banks[i].capability =
				(rs->value[4 + i] & 0x00000f00) >> 8;
		}

		/* extra */
		chl->dio.is_shared = chl->dio.capability & 0x01 ? 0x01 : 0x00;
		chl->dio.is_flash_store_cap =
				chl->dio.capability & 0x02 ? 0x01 : 0x00;
		chl->dio.di_filter_min = 0;
		chl->dio.di_filter_max = 0;

		for (i = 0; i < chl->dio.nr_bank; i++) {
			bank = &chl->dio.banks[i];
			bank->input_cap = bank->capability & 0x01 ? 0x01 : 0x00;
			bank->output_cap = bank->capability & 0x02 ? 0x01 : 0x00;
			bank->rising_trigger_cap =
				bank->capability & 0x04 ? 0x01 : 0x00;
			bank->falling_trigger_cap =
				bank->capability & 0x08 ? 0x01 : 0x00;
		}

		chl->supported = 1;
		break;
	case 0x01:
		chl->dio.mem_offset = rs->value[1];
		chl->dio.mem_size = rs->value[2];
		chl->dio.nr_bank = rs->value[3] & 0x000000ff;
		chl->dio.capability = (rs->value[3] & 0x00000300) >> 8;

		if (chl->dio.nr_bank > SDC_DIO_BANK_MAX)
			return;

		chl->dio.di_sampling_freq = rs->value[4];
		chl->dio.di_filter_lower_bound = rs->value[5];
		for (i = 0; i < chl->dio.nr_bank; i++) {
			chl->dio.banks[i].nr_io = rs->value[6 + i] & 0x000000ff;
			chl->dio.banks[i].capability =
				(rs->value[6 + i] & 0x00000f00) >> 8;
		}

		/* extra */
		chl->dio.is_shared = chl->dio.capability & 0x01 ? 0x01 : 0x00;
		chl->dio.is_flash_store_cap =
				chl->dio.capability & 0x02 ? 0x01 : 0x00;
		switch (chl->dio.di_sampling_freq) {
		case 2000:
			chl->dio.di_filter_max = (10 * 1000 * 1000) / 500;
			chl->dio.di_filter_min =
				chl->dio.di_filter_lower_bound / 500;
			break;
		default:
		case 500000:
			chl->dio.di_filter_max = (10 * 1000 * 1000) / 2;
			chl->dio.di_filter_min =
				chl->dio.di_filter_lower_bound / 2;
			break;
		}

		for (i = 0; i < chl->dio.nr_bank; i++) {
			bank = &chl->dio.banks[i];
			bank->input_cap = bank->capability & 0x01 ? 0x01 : 0x00;
			bank->output_cap = bank->capability & 0x02 ? 0x01 : 0x00;
			bank->rising_trigger_cap =
				bank->capability & 0x04 ? 0x01 : 0x00;
			bank->falling_trigger_cap =
				bank->capability & 0x08 ? 0x01 : 0x00;
		}

		chl->supported = 1;
		break;
	default:
		break;
	}

	io_mask = 0;
	io_shift_bits = 0;
	for (i = 0; i < chl->dio.nr_bank; i++) {
		bank = &chl->dio.banks[i];
		if (bank->nr_io == 0)
			continue;
		for (j = 0; j < bank->nr_io; j++)
			io_mask |= 1 << j;
		if (i > 0) {
			io_shift_bits += chl->dio.banks[i - 1].nr_io;
			io_mask = io_mask << io_shift_bits;
		}
		bank->io_mask = io_mask;
		bank->io_shift_bits = io_shift_bits;
	}
}

static void _board_get_hw_spi(struct config_regs *rs, struct sdc_chl *chl)
{
	struct sdc_spi_device *device;
	__u32 temp;
	int i;

	// rs->value[0] -> DW 2
	// rs->value[1] -> DW 3
	switch (chl->version) {
	case 0x00:
		chl->spi.mem_offset = rs->value[1];
		chl->spi.mem_size = rs->value[2];
		chl->spi.clk_sig = rs->value[3] & 0x00ffffff;
		chl->spi.clk_exp = (rs->value[3] & 0xffffff00) >> 24;
		chl->spi.nr_device = rs->value[4] & 0x000000ff;
		if (chl->spi.nr_device > SDC_SPI_DEVICE_MAX)
			return;

		for (i = 0; i < chl->spi.nr_device; i++) {
			device = &chl->spi.devices[i];

			temp = rs->value[5 + (i * 3)];
			device->type = temp & 0x000000ff;
			device->nr_gpio_input = (temp & 0x00000f00) >> 8;
			device->nr_gpio_output = (temp & 0x0000f000) >> 12;

			memset(device->name, 0, 17);

			temp = rs->value[6 + (i * 3)];
			device->name[0] = temp & 0x000000ff;
			device->name[1] = (temp & 0x0000ff00) >> 8;
			device->name[2] = (temp & 0x00ff0000) >> 16;
			device->name[3] = (temp & 0xff000000) >> 24;
			temp = rs->value[7 + (i * 3)];
			device->name[4] = temp & 0x000000ff;
			device->name[5] = (temp & 0x0000ff00) >> 8;
			device->name[6] = (temp & 0x00ff0000) >> 16;
			device->name[7] = (temp & 0xff000000) >> 24;

			device->name[8] = '\0';
		}

		chl->supported = 1;
		break;
	case 0x01:
		chl->spi.mem_offset = rs->value[1];
		chl->spi.mem_size = rs->value[2];
		chl->spi.clk_sig = rs->value[3] & 0x00ffffff;
		chl->spi.clk_exp = (rs->value[3] & 0xffffff00) >> 24;
		chl->spi.nr_device = rs->value[4] & 0x000000ff;
		if (chl->spi.nr_device > SDC_SPI_DEVICE_MAX)
			return;

		for (i = 0; i < chl->spi.nr_device; i++) {
			device = &chl->spi.devices[i];

			temp = rs->value[5 + (i * 5)];
			device->type = temp & 0x000000ff;
			device->nr_gpio_input = (temp & 0x00000f00) >> 8;
			device->nr_gpio_output = (temp & 0x0000f000) >> 12;

			memset(device->name, 0, 17);

			temp = rs->value[6 + (i * 5)];
			device->name[0] = temp & 0x000000ff;
			device->name[1] = (temp & 0x0000ff00) >> 8;
			device->name[2] = (temp & 0x00ff0000) >> 16;
			device->name[3] = (temp & 0xff000000) >> 24;
			temp = rs->value[7 + (i * 5)];
			device->name[4] = temp & 0x000000ff;
			device->name[5] = (temp & 0x0000ff00) >> 8;
			device->name[6] = (temp & 0x00ff0000) >> 16;
			device->name[7] = (temp & 0xff000000) >> 24;
			temp = rs->value[8 + (i * 5)];
			device->name[8] = temp & 0x000000ff;
			device->name[9] = (temp & 0x0000ff00) >> 8;
			device->name[10] = (temp & 0x00ff0000) >> 16;
			device->name[11] = (temp & 0xff000000) >> 24;
			temp = rs->value[9 + (i * 5)];
			device->name[12] = temp & 0x000000ff;
			device->name[13] = (temp & 0x0000ff00) >> 8;
			device->name[14] = (temp & 0x00ff0000) >> 16;
			device->name[15] = (temp & 0xff000000) >> 24;

			device->name[16] = '\0';
		}

		chl->supported = 1;
		break;
	default:
		break;
	}
}

static void _board_get_hw_can(struct config_regs *rs, struct sdc_chl *chl)
{
	// rs->value[0] -> DW 2
	// rs->value[1] -> DW 3
	switch (chl->version) {
	case 0x00:
		chl->can.mem_offset = rs->value[1];
		chl->can.mem_size = rs->value[2];
		chl->can.capability = rs->value[3];
		chl->can.tx_msg_queue_size = rs->value[4] & 0x0000ffff;
		chl->can.tx_event_queue_size = (rs->value[4] & 0xffff0000) >> 16;
		chl->can.rx_msg_queue_size = rs->value[5] & 0x0000ffff;
		chl->can.nr_rx_filter_group = (rs->value[5] & 0xff000000) >> 24;
		chl->can.sysclk_sig = rs->value[6] & 0x00ffffff;
		chl->can.sysclk_exp = (rs->value[6] & 0xffffff00) >> 24;
		chl->can.canrefclk_sig = rs->value[7] & 0x00ffffff;
		chl->can.canrefclk_exp = (rs->value[7] & 0xffffff00) >> 24;
		chl->supported = 1;
		break;
	default:
		break;
	}
}

static void _board_get_hw_parport(struct config_regs *rs, struct sdc_chl *chl)
{
	// rs->value[0] -> DW 2
	// rs->value[1] -> DW 3
	switch (chl->version) {
	case 0x00:
		chl->parport.mem_offset = rs->value[1];
		chl->parport.mem_size = rs->value[2];
		chl->supported = 1;
		break;
	default:
		break;
	}
}

static int _board_get_hw_controller(int fd, struct sdc_board *board,
								__u32 next_offset)
{
	struct config_regs rs;
	struct config_reg r;
	struct sdc_chl *chl;
	int i;

	for (i = 0; i < board->nr_controller; i++) {
		chl = &board->chls[i];

		memset(&r, 0, sizeof(r));
		r.offset = next_offset;
		if (ioctl(fd, CONFIG_GET_INFO_REG, &r) < 0)
			return ERR_IOCTL_FAILURE;

		chl->index = r.value & 0x000000ff;
		chl->type = (r.value & 0x0000ff00) >> 8;
		chl->version = (r.value & 0x00ff0000) >> 16;
		chl->cib_total_length = (r.value & 0xff000000) >> 24;

		memset(&rs, 0, sizeof(rs));
		rs.offset = next_offset + 1;
		rs.count = chl->cib_total_length - 1;
		if (ioctl(fd, CONFIG_GET_INFO_REGS, &rs) < 0)
			return ERR_IOCTL_FAILURE;

		chl->next_ptr = rs.value[0] & 0x0000ffff;
		next_offset = chl->next_ptr / 4;
		chl->resource_cap = (rs.value[0] & 0x00ff0000) >> 16;
		chl->event_header_type = (rs.value[0] & 0xff000000) >> 24;

		switch (chl->type) {
		case SDC_DEV_TYPE_CONFIG:
			_board_get_hw_config(&rs, chl);
			break;
		case SDC_DEV_TYPE_UART:
			_board_get_hw_uart(&rs, chl);
			break;
		case SDC_DEV_TYPE_DIO:
			_board_get_hw_dio(&rs, chl);
			break;
		case SDC_DEV_TYPE_SPI:
			_board_get_hw_spi(&rs, chl);
			break;
		case SDC_DEV_TYPE_CAN:
			_board_get_hw_can(&rs, chl);
			break;
		case SDC_DEV_TYPE_PARPORT:
			_board_get_hw_parport(&rs, chl);
			break;
		default:
			break;
		}
	}

	return 0;
}

static int _board_get_hw_info(int fd, int id, struct sdc_board *board)
{
	__u32 next_offset = 0;
	int ret;

	board->id = id;

	ret = _board_get_hw_basic(fd, board, &next_offset);
	if (ret != 0)
		return ret;

	ret = _board_get_hw_controller(fd, board, next_offset);
	if (ret != 0)
		return ret;

	return 0;
}

static int _config_assign_proc_info(struct sdc_board *board,
								struct proc_info *pinfo)
{
	struct proc_config *pconfig;
	int idx;

	if (!pinfo)
		return -1;

	/* channel 0 must be config and only one config in board */
	if (board->chls[0].type != SDC_DEV_TYPE_CONFIG)
		return -1;

	pconfig = NULL;
	for (idx = 0; idx < PROC_MAX; idx++) {
		if (pinfo->pconfig[idx].alive &&
			pinfo->pconfig[idx].b.index == board->chls[0].index &&
			idx == board->id) {
			pconfig = &pinfo->pconfig[idx];
			break;
		}
	}
	if (!pconfig)
		return -1;

	board->pci_bus = pconfig->b.pci_bus;
	board->irq = pconfig->b.irq;

	board->chls[0].pid = pconfig->b.pid;
	board->chls[0].line = pconfig->line;

	if (strlen(pconfig->name) > 0) {
		strcpy(board->chls[0].name, pconfig->name);
		snprintf(board->chls[0].device, NAME_LENGTH, "/dev/%s",
					pconfig->name);
	}

#if (DBG_CONFIG_LINE_PID)
	for (idx = 0; idx < board->nr_controller; idx++)
		if (board->chls[idx].type == SDC_DEV_TYPE_CONFIG)
			printf("CFG , ID:%d, I:%d, P:%d, L:%d, N:%s, D:%s\n",
				board->id, board->chls[idx].index, board->chls[idx].pid,
				board->chls[idx].line, board->chls[idx].name,
				board->chls[idx].device);
#endif

	return 0;
}

static int _uart_assign_proc_info(struct sdc_board *board,
								struct proc_info *pinfo)
{
	struct proc_uart *puart;
	int i, idx, has_chl = 0;

	if (!pinfo)
		return -1;

	for (i = 0; i < board->nr_controller; i++) {
		if (board->chls[i].type == SDC_DEV_TYPE_UART) {
			has_chl = 1;
			break;
		}
	}
	if (has_chl == 0)
		return 0;

	for (i = 0; i < board->nr_controller; i++) {
		if (board->chls[i].type != SDC_DEV_TYPE_UART)
			continue;
		puart = NULL;
		for (idx = 0; idx < PROC_MAX; idx++) {
			if (pinfo->puart[idx].alive &&
				pinfo->puart[idx].b.pci_bus == board->pci_bus &&
				pinfo->puart[idx].b.irq == board->irq &&
				pinfo->puart[idx].b.index == board->chls[i].index) {
				puart = &pinfo->puart[idx];
				break;
			}
		}
		if (!puart)
			return -1;

		board->chls[i].pid = puart->b.pid;
		board->chls[i].line = puart->line;

		if (strlen(puart->name) > 0) {
			strcpy(board->chls[i].name, puart->name);
			snprintf(board->chls[i].device, NAME_LENGTH, "/dev/%s",
				puart->name);
		}
	}

#if (DBG_UART_LINE_PID)
	for (idx = 0; idx < board->nr_controller; idx++)
		if (board->chls[idx].type == SDC_DEV_TYPE_UART)
			printf("UART, ID:%d, I:%d, P:%d, L:%d, N:%s, D:%s\n",
				board->id, board->chls[idx].index, board->chls[idx].pid,
				board->chls[idx].line, board->chls[idx].name,
				board->chls[idx].device);
#endif

	return 0;
}

static int _dio_assign_proc_info(struct sdc_board *board,
								struct proc_info *pinfo)
{
	struct proc_gpio *pgpio;
	struct proc_dio *pdio;
	int i, idx, has_chl = 0;

	if (!pinfo)
		return -1;

	for (i = 0; i < board->nr_controller; i++) {
		if (board->chls[i].type == SDC_DEV_TYPE_DIO) {
			has_chl = 1;
			break;
		}
	}
	if (has_chl == 0)
		return 0;

	for (i = 0; i < board->nr_controller; i++) {
		if (board->chls[i].type != SDC_DEV_TYPE_DIO)
			continue;
		pdio = NULL;
		pgpio = NULL;

		for (idx = 0; idx < PROC_MAX; idx++) {
			if (pinfo->pdio[idx].alive &&
				pinfo->pdio[idx].b.pci_bus == board->pci_bus &&
				pinfo->pdio[idx].b.irq == board->irq &&
				pinfo->pdio[idx].b.index == board->chls[i].index) {
				pdio = &pinfo->pdio[idx];
				break;
			}
			if (pinfo->pgpio[idx].alive &&
				pinfo->pgpio[idx].b.pci_bus == board->pci_bus &&
				pinfo->pgpio[idx].b.irq == board->irq &&
				pinfo->pgpio[idx].b.index == board->chls[i].index) {
				pgpio = &pinfo->pgpio[idx];
				break;
			}
		}
		if (pdio) {
			/* for DIO */
			board->chls[i].pid = pdio->b.pid;
			board->chls[i].line = pdio->line;

			if (strlen(pdio->name) > 0) {
				strcpy(board->chls[i].name, pdio->name);
				snprintf(board->chls[i].device, NAME_LENGTH, "/dev/%s",
					pdio->name);
			}
		} else if (pgpio) {
			/* for GPIO */
			board->chls[i].pid = pgpio->b.pid;
			board->chls[i].line = pgpio->line;

			if (strlen(pgpio->name) > 0) {
				strcpy(board->chls[i].name, pgpio->name);
				snprintf(board->chls[i].device, NAME_LENGTH, "/dev/%s",
					pgpio->name);
			}
		} else {
			return -1;
		}
	}

#if (DBG_DIO_LINE_PID)
	for (idx = 0; idx < board->nr_controller; idx++)
		if (board->chls[idx].type == SDC_DEV_TYPE_DIO)
			printf("DIO , ID:%d, I:%d, P:%d, L:%d, N:%s, D:%s\n",
				board->id, board->chls[idx].index, board->chls[idx].pid,
				board->chls[idx].line, board->chls[idx].name,
				board->chls[idx].device);
#endif

	return 0;
}

static int _spi_assign_proc_info(struct sdc_board *board,
								struct proc_info *pinfo)
{
	struct proc_spi *pspi;
	int i, j, idx, has_chl = 0;

	if (!pinfo)
		return -1;

	for (i = 0; i < board->nr_controller; i++) {
		if (board->chls[i].type == SDC_DEV_TYPE_SPI) {
			has_chl = 1;
			break;
		}
	}
	if (has_chl == 0)
		return 0;

	for (i = 0; i < board->nr_controller; i++) {
		if (board->chls[i].type != SDC_DEV_TYPE_SPI)
			continue;
		pspi = NULL;
		for (idx = 0; idx < PROC_MAX; idx++) {
			if (pinfo->pspi[idx].alive &&
				pinfo->pspi[idx].b.pci_bus == board->pci_bus &&
				pinfo->pspi[idx].b.irq == board->irq &&
				pinfo->pspi[idx].b.index == board->chls[i].index) {
				pspi = &pinfo->pspi[idx];
				break;
			}
		}
		if (!pspi)
			return -1;

		board->chls[i].pid = pspi->b.pid;

		for (j = 0; j < board->chls[i].spi.nr_device; j++) {
			board->chls[i].spi.devices[j].can_line = pspi->devices[j].line;
			if (strlen(pspi->devices[j].name) > 0)
				strcpy(board->chls[i].spi.devices[j].can_name,
					pspi->devices[j].name);
		}
	}

#if (DBG_SPI_CAN_LINE_PID)
	for (idx = 0; idx < board->nr_controller; idx++)
		if (board->chls[idx].type == SDC_DEV_TYPE_SPI)
			for (j = 0; j < board->chls[idx].spi.nr_device; j++)
				printf("SPI CAN , ID:%d, I:%d, P:%d, L:%d, N:%s, D:%s\n",
					board->id, board->chls[idx].index, board->chls[idx].pid,
					board->chls[idx].spi.devices[j].can_line,
					board->chls[idx].spi.devices[j].can_name,
					board->chls[idx].spi.devices[j].can_device);
#endif

	return 0;
}

static int _can_assign_proc_info(struct sdc_board *board,
								struct proc_info *pinfo)
{
	struct proc_can *pcan;
	int i, idx, has_chl = 0;

	if (!pinfo)
		return -1;

	for (i = 0; i < board->nr_controller; i++) {
		if (board->chls[i].type == SDC_DEV_TYPE_CAN) {
			has_chl = 1;
			break;
		}
	}
	if (has_chl == 0)
		return 0;

	for (i = 0; i < board->nr_controller; i++) {
		if (board->chls[i].type != SDC_DEV_TYPE_CAN)
			continue;
		pcan = NULL;
		for (idx = 0; idx < PROC_MAX; idx++) {
			if (pinfo->pcan[idx].alive &&
				pinfo->pcan[idx].b.pci_bus == board->pci_bus &&
				pinfo->pcan[idx].b.irq == board->irq &&
				pinfo->pcan[idx].b.index == board->chls[i].index) {
				pcan = &pinfo->pcan[idx];
				break;
			}
		}
		if (!pcan)
			return -1;

		board->chls[i].pid = pcan->b.pid;
		board->chls[i].line = pcan->line;

		if (strlen(pcan->name) > 0) {
			strcpy(board->chls[i].name, pcan->name);
			strcpy(board->chls[i].device, pcan->name);
		}
	}

#if (DBG_SDC_CAN_LINE_PID)
	for (idx = 0; idx < board->nr_controller; idx++)
		if (board->chls[idx].type == SDC_DEV_TYPE_CAN)
			printf("SDC CAN, ID:%d, I:%d, P:%d, L:%d, N:%s, D:%s\n",
				board->id, board->chls[idx].index, board->chls[idx].pid,
				board->chls[idx].line, board->chls[idx].name,
				board->chls[idx].device);
#endif

	return 0;
}

static int _parport_assign_proc_info(struct sdc_board *board,
								struct proc_info *pinfo)
{
	struct proc_parport *pparport;
	int i, idx, has_chl = 0;

	if (!pinfo)
		return -1;

	for (i = 0; i < board->nr_controller; i++) {
		if (board->chls[i].type == SDC_DEV_TYPE_PARPORT) {
			has_chl = 1;
			break;
		}
	}
	if (has_chl == 0)
		return 0;

	for (i = 0; i < board->nr_controller; i++) {
		if (board->chls[i].type != SDC_DEV_TYPE_PARPORT)
			continue;
		pparport = NULL;
		for (idx = 0; idx < PROC_MAX; idx++) {
			if (pinfo->pparport[idx].alive &&
				pinfo->pparport[idx].b.pci_bus == board->pci_bus &&
				pinfo->pparport[idx].b.irq == board->irq &&
				pinfo->pparport[idx].b.index == board->chls[i].index) {
				pparport = &pinfo->pparport[idx];
				break;
			}
		}
		if (!pparport)
			return -1;

		board->chls[i].pid = pparport->b.pid;
		board->chls[i].line = pparport->line;

		if (strlen(pparport->name) > 0) {
			strcpy(board->chls[i].name, pparport->name);
			snprintf(board->chls[i].device, NAME_LENGTH, "/dev/%s",
				pparport->name);
		}
	}

#if (DBG_PARPORT_LINE_PID)
	for (idx = 0; idx < board->nr_controller; idx++)
		if (board->chls[idx].type == SDC_DEV_TYPE_PARPORT)
			printf("PARPORT, ID:%d, I:%d, P:%d, L:%d, N:%s, D:%s\n",
				board->id, board->chls[idx].index, board->chls[idx].pid,
				board->chls[idx].line, board->chls[idx].name,
				board->chls[idx].device);
#endif

	return 0;
}

static void _init_board_list(struct xlist *list, struct proc_info *pinfo)
{
	struct sdc_board *board;
	char buff[32];
	int i, fd;

	xlist_init(list);

	for (i = 0; i < SDC_BOARD_MAX; i++) {
		snprintf(buff, sizeof(buff), "/dev/%s%d", SDC_CONFIG_DEV_NAME, i);

		fd = open(buff, O_RDONLY);
		if (fd < 0)
			continue;

		board = malloc(sizeof(*board));
		if (!board) {
			close(fd);
			continue;
		}

		memset(board, 0, sizeof(*board));
		_board_init(board);

		board->error = _board_get_hw_info(fd, i, board);
		close(fd);

		xlist_init(&board->entry);
		xlist_insert_tail(list, &board->entry);

		if (board->error < 0)
			continue;

		_config_assign_proc_info(board, pinfo);
		_uart_assign_proc_info(board, pinfo);
		_dio_assign_proc_info(board, pinfo);
		_spi_assign_proc_info(board, pinfo);
		_can_assign_proc_info(board, pinfo);
		_parport_assign_proc_info(board, pinfo);
	}
}

static void _free_board_list(struct xlist *list)
{
	struct sdc_board *board;
	struct xlist *entry;

	while (!xlist_empty(list)) {
		entry = xlist_get_next(list);
		if (entry) {
			board = SDC_BOARD_PTR(entry);
			if (board) {
				xlist_remove_entry(&board->entry);
				free(board);
			}
		}
	}
}

void board_list_init(struct xlist *list)
{
	struct proc_info *pinfo;

	pinfo = malloc(sizeof(*pinfo));
	if (pinfo) {
		_proc_init(pinfo);

		_init_board_list(list, pinfo);

		free(pinfo);
	}
}

void board_list_free(struct xlist *list)
{
	_free_board_list(list);
}

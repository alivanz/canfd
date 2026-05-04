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

int board_display_all(void)
{
	struct sdc_board *board;
	struct xlist *entry;

	if (xlist_empty(&GB_board_list)) {
		printf("No SDC board in list\n");
		return 0;
	}

	entry = GB_board_list.next;
	while (entry && entry != &GB_board_list) {
		board = SDC_BOARD_PTR(entry);
		if (board) {
			printf("ID%d: ", board->id);
			if (board->error) {
				printf("error:%d\n", board->error);
			} else {
				printf("model=%s, firmware=%d.%d, pci_bus=%d, irq=%d\n",
					board->model_name, board->major_version, board->minor_version,
					board->pci_bus, board->irq);
			}
		}
		entry = entry->next;
		board = NULL;
	}

	return 0;
}

int board_display_detail(char *parm1)
{
	struct sdc_spi_device *device;
	struct sdc_dio_bank *bank;
	struct sdc_board *match;
	struct sdc_chl *chl;
	struct xlist *entry;
	int id, i, j;

	for (i = 0; i < strlen(parm1); i++) {
		if (parm1[i] < '0' || parm1[i] > '9') {
			printf("Invalid ID string\n");
			return -EINVAL;
		}
	}

	if (xlist_empty(&GB_board_list)) {
		printf("No SDC board in list\n");
		return -ENODEV;
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
		return -ENODEV;
	}

	printf("ID%d: ", match->id);
	if (match->error) {
		printf("error:%d\n", match->error);

	} else {
		printf("model=%s, firmware=%d.%d, pci_bus=%d, irq=%d\n",
			match->model_name, match->major_version, match->minor_version,
			match->pci_bus, match->irq);

		for (i = 0; i < match->nr_controller; i++) {
			chl = &match->chls[i];

			if (chl->type == SDC_DEV_TYPE_CONFIG && chl->supported) {
				printf("   CONFIG(%d):\n", chl->index);
				printf("      ver=x%02X, pid=%d, /dev=%s\n",
					chl->version, chl->pid, chl->name);
				printf("      brand=%d, model=%d\n",
					chl->config.brand, chl->config.model);
			} else if (chl->type == SDC_DEV_TYPE_CONFIG) {
				printf("   CONFIG(%d):\n", chl->index);
				printf("      ver=x%02X, UNSUPPORT!!\n", chl->version);
			}

			if (chl->type == SDC_DEV_TYPE_UART && chl->supported) {
				printf("   UART(%d):\n", chl->index);
				printf("      ver=x%02X, pid=%d, /dev=%s\n",
					chl->version, chl->pid, chl->name);
				printf("      fifo=%d, cap=", chl->uart.rx_fifo_size);
				switch (chl->uart.capability & 0x07) {
				case 0x01:
					printf("rs232\n");
					break;
				case 0x02:
					printf("rs422\n");
					break;
				case 0x04:
					printf("rs485\n");
					break;
				case 0x06:
					printf("rs422|rs485\n");
					break;
				case 0x07:
					printf("rs232|rs422|rs485\n");
					break;
				default:
					printf("unknow(x%08X)\n", chl->uart.capability);
					break;
				}
			} else if (chl->type == SDC_DEV_TYPE_UART) {
				printf("   UART(%d):\n", chl->index);
				printf("      ver=x%02X, UNSUPPORT!!\n", chl->version);
			}

			if (chl->type == SDC_DEV_TYPE_DIO && chl->supported) {
				printf("   DIO(%d):\n", chl->index);
				printf("      ver=x%02X, pid=%d, /dev=%s\n",
					chl->version, chl->pid, chl->name);
				printf("      cap=x%02X, nr_bank=%d\n",
					chl->dio.capability, chl->dio.nr_bank);
				for (j = 0; j < chl->dio.nr_bank; j++) {
					bank = &chl->dio.banks[j];
					printf("         B%d: nr_io=%d, cap=", j,
						bank->nr_io);
					switch (bank->capability & 0x03) {
					case 0x01:
						printf("input\n");
						break;
					case 0x02:
						printf("output\n");
						break;
					case 0x03:
						printf("input|output\n");
						break;
					default:
						printf("unknow(x%02X)\n", bank->capability);
						break;
					}
				}
			} else if (chl->type == SDC_DEV_TYPE_DIO) {
				printf("   DIO(%d):\n", chl->index);
				printf("      ver=x%02X, UNSUPPORT!!\n", chl->version);
			}

			if (chl->type == SDC_DEV_TYPE_SPI && chl->supported) {
				printf("   SPI(%d):\n", chl->index);
				printf("      ver=x%02X, pid=%d\n",
					chl->version, chl->pid);
				printf("      nr_device=%d\n",
					chl->spi.nr_device);
				for (j = 0; j < chl->spi.nr_device; j++) {
					device = &chl->spi.devices[j];
					printf("         D%d: type=%d, nr_gpio=%d;%d, ",
						j, device->type, device->nr_gpio_input,
						device->nr_gpio_output);
					printf("name=%s, dev=%s\n", device->name,
						device->can_name);
				}
			} else if (chl->type == SDC_DEV_TYPE_SPI) {
				printf("   SPI(%d):\n", chl->index);
				printf("      ver=x%02X, UNSUPPORT!!\n", chl->version);
			}

			if (chl->type == SDC_DEV_TYPE_CAN && chl->supported) {
				printf("   CAN(%d):\n", chl->index);
				printf("      ver=x%02X, pid=%d, dev=%s\n",
					chl->version, chl->pid, chl->name);
				printf("      cap=x%08X, filters=%d\n",
					chl->can.capability, chl->can.nr_rx_filter_group);
				printf("      tx_msg_q_size=%d, tx_event_q_size=%d, rx_msg_q_size=%d\n",
					chl->can.tx_msg_queue_size,
					chl->can.tx_event_queue_size,
					chl->can.rx_msg_queue_size);
			} else if (chl->type == SDC_DEV_TYPE_CAN) {
				printf("   CAN(%d):\n", chl->index);
				printf("      ver=x%02X, UNSUPPORT!!\n", chl->version);
			}

			if (chl->type == SDC_DEV_TYPE_PARPORT && chl->supported) {
				printf("   PARPORT(%d):\n", chl->index);
				printf("      ver=x%02X, pid=%d, /dev=%s\n",
					chl->version, chl->pid, chl->name);
			} else if (chl->type == SDC_DEV_TYPE_PARPORT) {
				printf("   PARPORT(%d):\n", chl->index);
				printf("      ver=x%02X, UNSUPPORT!!\n", chl->version);
			}
		}
	}

	return 0;
}

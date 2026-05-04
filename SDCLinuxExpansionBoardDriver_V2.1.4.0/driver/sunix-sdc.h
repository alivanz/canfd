/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sunix-sdc - SUNIX SDC PCIe board core support
 *
 * Copyright (c) 2025 SUNIX Co., Ltd. <info@sunix.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 *
 */

#ifndef _SUNIX_SDC_H_
#define _SUNIX_SDC_H_

#include <linux/types.h>

#define SDC_DRIVER_VERSION		"2.1.1.0"

#define SDC_DEV_TYPE_CONFIG		0x00
#define SDC_DEV_TYPE_UART		0x01
#define SDC_DEV_TYPE_DIO		0x02
#define SDC_DEV_TYPE_SPI		0x03
#define SDC_DEV_TYPE_CAN		0x04
#define SDC_DEV_TYPE_PARPORT	0x05

#define SDC_SPI_DEVICE_FLASH	0x00
#define SDC_SPI_DEVICE_ADC		0x01
#define SDC_SPI_DEVICE_DAC		0x02
#define SDC_SPI_DEVICE_CAN		0x03

#define SDC_DIO_BANK_MAX		32
#define SDC_SPI_DEVICE_MAX		16

struct sdc_board_platdata {
	u8 major_version;
	u8 minor_version;
	u8 nr_controller;
	char model_name[17];

	int pci_bus;
	int irq;
};

struct sdc_config_platdata {
	struct sdc_board_platdata board;
	resource_size_t mem_offset;
	resource_size_t mem_size;

	u8 index;
	u8 version;
	u8 model;
	u8 brand;
};

struct sdc_uart_platdata {
	struct sdc_board_platdata board;
	resource_size_t io_offset;
	resource_size_t io_size;

	u8 index;
	u8 version;
	u16 fifo_size;
	u32 capability;
	int clk_rate;
};

struct sdc_dio_bank_platdata {
	u8 nr_io;
	u8 capability;
	int cap_input;
	int cap_output;
	int cap_rising_trigger;
	int cap_falling_trigger;
	u32 io_mask;
	u32 io_shift_bits;
};

struct sdc_dio_platdata {
	struct sdc_board_platdata board;
	resource_size_t mem_offset;
	resource_size_t mem_size;

	u8 index;
	u8 version;
	u8 capability;
	int cap_samedirection;
	int cap_storeflash;
	u8 nr_bank;
	u32 di_sampling_freq;
	u32 di_filter_lower_bound;
	u32 di_filter_min;
	u32 di_filter_max;
	struct sdc_dio_bank_platdata banks[SDC_DIO_BANK_MAX];
};

struct sdc_spi_device_platdata {
	u8 cs;
	u8 type;
	u8 nr_gpio_input;
	u8 nr_gpio_output;
	char name[17];

	u32 can_irq;
	u32 can_termination;
};

struct sdc_spi_platdata {
	struct sdc_board_platdata board;
	resource_size_t mem_offset;
	resource_size_t mem_size;

	u8 index;
	u8 version;
	u8 nr_device;
	int clk_rate;
	struct sdc_spi_device_platdata devices[SDC_SPI_DEVICE_MAX];
};

struct sdc_can_platdata {
	struct sdc_board_platdata board;
	resource_size_t mem_offset;
	resource_size_t mem_size;

	u8 index;
	u8 version;
	u32 capability;
	u16 tx_msg_queue_size;
	u16 tx_event_queue_size;
	u16 rx_msg_queue_size;
	u16 nr_rx_filter_group;
	int sys_clk_rate;
	int ref_clk_rate;
};

struct sdc_parport_platdata {
	struct sdc_board_platdata board;
	resource_size_t mem_offset;
	resource_size_t mem_size;

	u8 index;
	u8 version;
};

#endif

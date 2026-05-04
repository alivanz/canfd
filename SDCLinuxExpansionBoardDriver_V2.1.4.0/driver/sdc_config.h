/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sdc_config - SUNIX SDC configuration controller driver
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

#ifndef _SDC_CONFIG_H_
#define _SDC_CONFIG_H_

#include "sunix-sdc.h"

/* Memory space 512 byte */
#define CONFIG_REG_MAX			128
/* NVM 256 byte */
#define CONFIG_NVM_MAX			64

struct config_reg {
	__u32 offset;
	__u32 value;
};

struct config_regs {
	__u32 offset;
	__u32 count;
	__u32 value[CONFIG_REG_MAX];
};

struct config_dio {
	__u32 direction;
	__u32 output_initial;
	__u32 nr_bank;
	__u32 bank[SDC_DIO_BANK_MAX];
};

struct config_bitstream {
	__u32 addr;
	__u32 len;
	__u32 data[CONFIG_NVM_MAX];
};

#define CONFIG_GET_INFO_REG		_IOWR('c', 1, struct config_reg *)
#define CONFIG_GET_INFO_REGS	_IOWR('c', 2, struct config_regs *)
#define CONFIG_DIO_ERASE		_IO('c', 3)
#define CONFIG_DIO_SET			_IOW('c', 4, struct config_dio *)
#define CONFIG_DIO_GET			_IOWR('c', 5, struct config_dio *)
#define CONFIG_BITSTREAM_ERASE	_IOW('c', 6, struct config_bitstream *)
#define CONFIG_BITSTREAM_SET	_IOW('c', 7, struct config_bitstream *)
#define CONFIG_BITSTREAM_GET	_IOWR('c', 8, struct config_bitstream *)

struct config_dio_settings {
	int pci_bus;
	struct config_dio settings;
};

extern int sdc_config_process_dio_settings(struct config_dio_settings *s);

#endif

// SPDX-License-Identifier: GPL-2.0
/*
 * sunix-sdc - SUNIX SDC PCIe board core driver
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/version.h>
#ifndef KERNEL_VERSION
#define KERNEL_VERSION(ver, rel, seq) ((ver << 16) | (rel << 8) | seq)
#endif
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/property.h>
#include <linux/pci.h>
#include <linux/mfd/core.h>
#include "sunix-sdc.h"

#define DRV_NAME				"sunix_sdc"

#define SDC_CONFIG_MAX			1
#define SDC_UART_MAX			32
#define SDC_DIO_MAX				32
#define SDC_SPI_MAX				8
#define SDC_CAN_MAX				8
#define SDC_PARPORT_MAX			8

struct sdc_config {
	struct sdc_config_platdata pdata;
	struct resource res[3];
	int supported;
};

struct sdc_uart {
	struct sdc_uart_platdata pdata;
	struct resource res[2];
	int supported;
};

struct sdc_dio {
	struct sdc_dio_platdata pdata;
	struct resource res[3];
	int supported;
};

struct sdc_spi {
	struct sdc_spi_platdata pdata;
	struct resource res[3];
	int supported;
};

struct sdc_can {
	struct sdc_can_platdata pdata;
	struct resource res[3];
	int supported;
};

struct sdc_parport {
	struct sdc_parport_platdata pdata;
	struct resource res[3];
	int supported;
};

struct sdc_board {
	struct pci_dev *pdev;
	struct sdc_board_platdata pdata;
	int nr_ctlr_supported;

	int nr_config;
	struct sdc_config config;
	int nr_uart;
	struct sdc_uart uart[SDC_UART_MAX];
	int nr_dio;
	struct sdc_dio dio[SDC_DIO_MAX];
	int nr_spi;
	struct sdc_spi spi[SDC_SPI_MAX];
	int nr_can;
	struct sdc_can can[SDC_CAN_MAX];
	int nr_parport;
	struct sdc_parport parport[SDC_PARPORT_MAX];
};

static u32 sdc_readl(void __iomem *base, int ctlr_offset, int reg_offset)
{
	return readl(base + ctlr_offset + (reg_offset * 4));
}

static int sdc_get_basic_info(struct sdc_board *priv, void __iomem *base,
								int *next_offset)
{
	struct sdc_board_platdata *pdata = &priv->pdata;
	int offset;
	u32 temp;
	int i, j;

	temp = sdc_readl(base, 0, 0);
	pdata->major_version = temp & GENMASK(7, 0);
	pdata->minor_version = (temp & GENMASK(15, 8)) >> 8;
	pdata->nr_controller = (temp & GENMASK(23, 16)) >> 16;
	if (pdata->nr_controller < 1)
		return -EINVAL;

	temp = sdc_readl(base, 0, 1);
	offset = temp & GENMASK(15, 0);
	*next_offset = offset;

	j = 0;
	for (i = 0; i < 4; i++) {
		temp = sdc_readl(base, 0, 2 + i);
		pdata->model_name[j++] = temp & GENMASK(7, 0);
		pdata->model_name[j++] = (temp & GENMASK(15, 8)) >> 8;
		pdata->model_name[j++] = (temp & GENMASK(23, 16)) >> 16;
		pdata->model_name[j++] = (temp & GENMASK(31, 24)) >> 24;
	}

	pdata->pci_bus = priv->pdev->bus->number;
	pdata->irq = priv->pdev->irq;

	return 0;
}

static int sdc_get_config_info(struct sdc_config *config, int pci_bus,
								void __iomem *base, int offset)
{
	struct sdc_config_platdata *pdata = &config->pdata;
	u32 temp;

	temp = sdc_readl(base, offset, 0);
	pdata->index = temp & GENMASK(7, 0);
	pdata->version = (temp & GENMASK(23, 16)) >> 16;

	switch (pdata->version) {
	case 0x00:
		pdata->mem_offset = sdc_readl(base, offset, 2);
		pdata->mem_size = sdc_readl(base, offset, 3);

		temp = sdc_readl(base, offset, 4);
		pdata->model = temp & GENMASK(7, 0);
		pdata->brand = (temp & GENMASK(15, 8)) >> 8;
		config->supported = 1;
		break;
	default:
		pr_warn("unsupport CONFIG version x%02X (pci_bus:%d)\n",
			pdata->version, pci_bus);
		return -EINVAL;
	}

	return 0;
}

#if SDC_CREATE_UART
static int sdc_get_uart_info(struct sdc_uart *uart, int pci_bus,
								void __iomem *base, int offset)
{
	struct sdc_uart_platdata *pdata = &uart->pdata;
	int significand;
	int exponent;
	int clk_rate;
	u32 temp;
	int i;

	temp = sdc_readl(base, offset, 0);
	pdata->index = temp & GENMASK(7, 0);
	pdata->version = (temp & GENMASK(23, 16)) >> 16;

	switch (pdata->version) {
	case 0x00:
	case 0x01:
		temp = sdc_readl(base, offset, 2);
		pdata->io_offset = temp & GENMASK(23, 0);
		pdata->io_size = (temp & GENMASK(31, 24)) >> 24;

		temp = sdc_readl(base, offset, 5);
		pdata->fifo_size = (temp & GENMASK(31, 16)) >> 16;

		temp = sdc_readl(base, offset, 6);
		significand = temp & GENMASK(23, 0);
		exponent = (temp & GENMASK(31, 24)) >> 24;
		clk_rate = 1;
		for (i = 0; i < exponent; i++)
			clk_rate *= 10;
		pdata->clk_rate = clk_rate * significand;

		pdata->capability = sdc_readl(base, offset, 7);
		uart->supported = 1;
		break;
	default:
		pr_warn("unsupport UART version x%02X (pci_bus:%d)\n",
			pdata->version, pci_bus);
		return -EINVAL;
	}

	return 0;
}
#endif

#if SDC_CREATE_DIO || SDC_CREATE_GPIO
static int sdc_get_dio_info(struct sdc_dio *dio, int pci_bus,
								void __iomem *base,	int offset)
{
	struct sdc_dio_platdata *pdata = &dio->pdata;
	u32 io_shift_bits, io_mask;
	u32 temp;
	int i;

	temp = sdc_readl(base, offset, 0);
	pdata->index = temp & GENMASK(7, 0);
	pdata->version = (temp & GENMASK(23, 16)) >> 16;

	switch (pdata->version) {
	case 0x00:
		pdata->mem_offset = sdc_readl(base, offset, 2);
		pdata->mem_size = sdc_readl(base, offset, 3);

		temp = sdc_readl(base, offset, 4);
		pdata->nr_bank = temp & GENMASK(7, 0);
		pdata->capability = (temp & GENMASK(9, 8)) >> 8;

		if (pdata->capability & 0x01)
			pdata->cap_samedirection = 1;
		else
			pdata->cap_samedirection = 0;

		if (pdata->capability & 0x02)
			pdata->cap_storeflash = 1;
		else
			pdata->cap_storeflash = 0;

		if (pdata->nr_bank > SDC_DIO_BANK_MAX)
			return -EINVAL;

		pdata->di_sampling_freq = 0;
		pdata->di_filter_lower_bound = 0;
		pdata->di_filter_min = 0;
		pdata->di_filter_max = 0;

		for (i = 0; i < pdata->nr_bank; i++) {
			temp = sdc_readl(base, offset, 5 + i);
			pdata->banks[i].nr_io = temp & GENMASK(7, 0);
			pdata->banks[i].capability = (temp & GENMASK(11, 8)) >> 8;
		}
		dio->supported = 1;
		break;
	case 0x01:
		pdata->mem_offset = sdc_readl(base, offset, 2);
		pdata->mem_size = sdc_readl(base, offset, 3);

		temp = sdc_readl(base, offset, 4);
		pdata->nr_bank = temp & GENMASK(7, 0);
		pdata->capability = (temp & GENMASK(9, 8)) >> 8;

		if (pdata->capability & 0x01)
			pdata->cap_samedirection = 1;
		else
			pdata->cap_samedirection = 0;

		if (pdata->capability & 0x02)
			pdata->cap_storeflash = 1;
		else
			pdata->cap_storeflash = 0;

		if (pdata->nr_bank > SDC_DIO_BANK_MAX)
			return -EINVAL;

		pdata->di_sampling_freq = sdc_readl(base, offset, 5);
		pdata->di_filter_lower_bound = sdc_readl(base, offset, 6);
		switch (pdata->di_sampling_freq) {
		case 2000:
			pdata->di_filter_max = (10 * 1000 * 1000) / 500;
			pdata->di_filter_min = pdata->di_filter_lower_bound / 500;
			break;
		default:
		case 500000:
			pdata->di_filter_max = (10 * 1000 * 1000) / 2;
			pdata->di_filter_min = pdata->di_filter_lower_bound / 2;
			break;
		}

		for (i = 0; i < pdata->nr_bank; i++) {
			temp = sdc_readl(base, offset, 7 + i);
			pdata->banks[i].nr_io = temp & GENMASK(7, 0);
			pdata->banks[i].capability = (temp & GENMASK(11, 8)) >> 8;
		}
		dio->supported = 1;
		break;
	default:
		pr_warn("unsupport DIO version x%02X (pci_bus:%d)\n",
			pdata->version, pci_bus);
		return -EINVAL;
	}

	io_shift_bits = 0;
	for (i = 0; i < pdata->nr_bank; i++) {
		if (pdata->banks[i].capability & 0x01)
			pdata->banks[i].cap_input = 1;
		else
			pdata->banks[i].cap_input = 0;

		if (pdata->banks[i].capability & 0x02)
			pdata->banks[i].cap_output = 1;
		else
			pdata->banks[i].cap_output = 0;

		if (pdata->banks[i].capability & 0x04)
			pdata->banks[i].cap_rising_trigger = 1;
		else
			pdata->banks[i].cap_rising_trigger = 0;

		if (pdata->banks[i].capability & 0x08)
			pdata->banks[i].cap_falling_trigger = 1;
		else
			pdata->banks[i].cap_falling_trigger = 0;

		if (pdata->banks[i].nr_io == 0)
			continue;
		io_mask = GENMASK(pdata->banks[i].nr_io - 1, 0);
		if (i > 0) {
			io_shift_bits += pdata->banks[i - 1].nr_io;
			io_mask = io_mask << io_shift_bits;
		}
		pdata->banks[i].io_mask = io_mask;
		pdata->banks[i].io_shift_bits = io_shift_bits;
	}

	return 0;
}
#endif

#if SDC_CREATE_SPI
static int sdc_get_spi_info(struct sdc_spi *spi, int pci_bus,
								void __iomem *base,	int offset)
{
	struct sdc_spi_device_platdata *pdevice;
	struct sdc_spi_platdata *pdata = &spi->pdata;
	int significand;
	int exponent;
	int clk_rate;
	u32 temp, nr_input, nr_output;
	int i;

	temp = sdc_readl(base, offset, 0);
	pdata->index = temp & GENMASK(7, 0);
	pdata->version = (temp & GENMASK(23, 16)) >> 16;

	switch (pdata->version) {
	case 0x00:
		pdata->mem_offset = sdc_readl(base, offset, 2);
		pdata->mem_size = sdc_readl(base, offset, 3);

		temp = sdc_readl(base, offset, 4);
		significand = temp & GENMASK(23, 0);
		exponent = (temp & GENMASK(31, 24)) >> 24;
		clk_rate = 1;
		for (i = 0; i < exponent; i++)
			clk_rate *= 10;
		pdata->clk_rate = clk_rate * significand;

		temp = sdc_readl(base, offset, 5);
		pdata->nr_device = temp & GENMASK(7, 0);

		if (pdata->nr_device > SDC_SPI_DEVICE_MAX)
			return -EINVAL;

		nr_input = nr_output = 0;
		for (i = 0; i < pdata->nr_device; i++) {
			pdevice = &pdata->devices[i];

			pdevice->cs = 1 << i;

			temp = sdc_readl(base, offset, 6 + (i * 3));
			pdevice->type = temp & GENMASK(7, 0);
			pdevice->nr_gpio_input = (temp & GENMASK(11, 8)) >> 8;
			pdevice->nr_gpio_output = (temp & GENMASK(15, 12)) >> 12;

			temp = sdc_readl(base, offset, 7 + (i * 3));
			pdevice->name[0] = temp & GENMASK(7, 0);
			pdevice->name[1] = (temp & GENMASK(15, 8)) >> 8;
			pdevice->name[2] = (temp & GENMASK(23, 16)) >> 16;
			pdevice->name[3] = (temp & GENMASK(31, 24)) >> 24;

			temp = sdc_readl(base, offset, 8 + (i * 3));
			pdevice->name[4] = temp & GENMASK(7, 0);
			pdevice->name[5] = (temp & GENMASK(15, 8)) >> 8;
			pdevice->name[6] = (temp & GENMASK(23, 16)) >> 16;
			pdevice->name[7] = (temp & GENMASK(31, 24)) >> 24;

			if (pdevice->type == SDC_SPI_DEVICE_CAN) {
				if (pdevice->nr_gpio_input > 0) {
					temp = GENMASK(pdevice->nr_gpio_input - 1, 0) << nr_input;
					pdevice->can_irq = temp;
				} else {
					pdevice->can_irq = 0;
				}

				if (pdevice->nr_gpio_output > 0) {
					temp = GENMASK(pdevice->nr_gpio_output - 1, 0) << nr_output;
					pdevice->can_termination = temp;
				} else {
					pdevice->can_termination = 0;
				}
			}

			nr_input += pdevice->nr_gpio_input;
			nr_output += pdevice->nr_gpio_output;
			if (nr_input > 32 || nr_output > 32)
				return -EINVAL;
		}
		spi->supported = 1;
		break;
	case 0x01:
		pdata->mem_offset = sdc_readl(base, offset, 2);
		pdata->mem_size = sdc_readl(base, offset, 3);

		temp = sdc_readl(base, offset, 4);
		significand = temp & GENMASK(23, 0);
		exponent = (temp & GENMASK(31, 24)) >> 24;
		clk_rate = 1;
		for (i = 0; i < exponent; i++)
			clk_rate *= 10;
		pdata->clk_rate = clk_rate * significand;

		temp = sdc_readl(base, offset, 5);
		pdata->nr_device = temp & GENMASK(7, 0);

		if (pdata->nr_device > SDC_SPI_DEVICE_MAX)
			return -EINVAL;

		nr_input = nr_output = 0;
		for (i = 0; i < pdata->nr_device; i++) {
			pdevice = &pdata->devices[i];

			pdevice->cs = 1 << i;

			temp = sdc_readl(base, offset, 6 + (i * 5));
			pdevice->type = temp & GENMASK(7, 0);
			pdevice->nr_gpio_input = (temp & GENMASK(11, 8)) >> 8;
			pdevice->nr_gpio_output = (temp & GENMASK(15, 12)) >> 12;

			temp = sdc_readl(base, offset, 7 + (i * 5));
			pdevice->name[0] = temp & GENMASK(7, 0);
			pdevice->name[1] = (temp & GENMASK(15, 8)) >> 8;
			pdevice->name[2] = (temp & GENMASK(23, 16)) >> 16;
			pdevice->name[3] = (temp & GENMASK(31, 24)) >> 24;

			temp = sdc_readl(base, offset, 8 + (i * 5));
			pdevice->name[4] = temp & GENMASK(7, 0);
			pdevice->name[5] = (temp & GENMASK(15, 8)) >> 8;
			pdevice->name[6] = (temp & GENMASK(23, 16)) >> 16;
			pdevice->name[7] = (temp & GENMASK(31, 24)) >> 24;

			temp = sdc_readl(base, offset, 9 + (i * 5));
			pdevice->name[8] = temp & GENMASK(7, 0);
			pdevice->name[9] = (temp & GENMASK(15, 8)) >> 8;
			pdevice->name[10] = (temp & GENMASK(23, 16)) >> 16;
			pdevice->name[11] = (temp & GENMASK(31, 24)) >> 24;

			temp = sdc_readl(base, offset, 10 + (i * 5));
			pdevice->name[12] = temp & GENMASK(7, 0);
			pdevice->name[13] = (temp & GENMASK(15, 8)) >> 8;
			pdevice->name[14] = (temp & GENMASK(23, 16)) >> 16;
			pdevice->name[15] = (temp & GENMASK(31, 24)) >> 24;

			if (pdevice->type == SDC_SPI_DEVICE_CAN) {
				if (pdevice->nr_gpio_input > 0) {
					temp = GENMASK(pdevice->nr_gpio_input - 1, 0) << nr_input;
					pdevice->can_irq = temp;
				} else {
					pdevice->can_irq = 0;
				}

				if (pdevice->nr_gpio_output > 0) {
					temp = GENMASK(pdevice->nr_gpio_output - 1, 0) << nr_output;
					pdevice->can_termination = temp;
				} else {
					pdevice->can_termination = 0;
				}
			}

			nr_input += pdevice->nr_gpio_input;
			nr_output += pdevice->nr_gpio_output;
			if (nr_input > 32 || nr_output > 32)
				return -EINVAL;
		}
		spi->supported = 1;
		break;
	default:
		pr_warn("unsupport SPI version x%02X (pci_bus:%d)\n",
			pdata->version, pci_bus);
		return -EINVAL;
	}

	return 0;
}
#endif

#if SDC_CREATE_CAN
static int sdc_get_can_info(struct sdc_can *can, int pci_bus,
								void __iomem *base,	int offset)
{
	struct sdc_can_platdata *pdata = &can->pdata;
	int significand;
	int exponent;
	int clk_rate;
	u32 temp;
	int i;

	temp = sdc_readl(base, offset, 0);
	pdata->index = temp & GENMASK(7, 0);
	pdata->version = (temp & GENMASK(23, 16)) >> 16;

	switch (pdata->version) {
	case 0x00:
		pdata->mem_offset = sdc_readl(base, offset, 2);
		pdata->mem_size = sdc_readl(base, offset, 3);

		pdata->capability = sdc_readl(base, offset, 4);

		temp = sdc_readl(base, offset, 5);
		pdata->tx_msg_queue_size = temp & GENMASK(15, 0);
		pdata->tx_event_queue_size = (temp & GENMASK(31, 16)) >> 16;

		temp = sdc_readl(base, offset, 6);
		pdata->rx_msg_queue_size = temp & GENMASK(15, 0);
		pdata->nr_rx_filter_group = (temp & GENMASK(31, 24)) >> 16;

		temp = sdc_readl(base, offset, 7);
		significand = temp & GENMASK(23, 0);
		exponent = (temp & GENMASK(31, 24)) >> 24;
		clk_rate = 1;
		for (i = 0; i < exponent; i++)
			clk_rate *= 10;
		pdata->sys_clk_rate = clk_rate * significand;

		temp = sdc_readl(base, offset, 8);
		significand = temp & GENMASK(23, 0);
		exponent = (temp & GENMASK(31, 24)) >> 24;
		clk_rate = 1;
		for (i = 0; i < exponent; i++)
			clk_rate *= 10;
		pdata->ref_clk_rate = clk_rate * significand;

		can->supported = 1;
		break;
	default:
		pr_warn("unsupport CAN version x%02X (pci_bus:%d)\n",
			pdata->version, pci_bus);
		return -EINVAL;
	}

	return 0;
}
#endif

#if SDC_CREATE_PARPORT
static int sdc_get_parport_info(struct sdc_parport *parport, int pci_bus,
								void __iomem *base,	int offset)
{
	struct sdc_parport_platdata *pdata = &parport->pdata;
	u32 temp;

	temp = sdc_readl(base, offset, 0);
	pdata->index = temp & GENMASK(7, 0);
	pdata->version = (temp & GENMASK(23, 16)) >> 16;

	switch (pdata->version) {
	case 0x00:
		pdata->mem_offset = sdc_readl(base, offset, 2);
		pdata->mem_size = sdc_readl(base, offset, 3);

		parport->supported = 1;
		break;
	default:
		pr_warn("unsupport PARPORT version x%02X (pci_bus:%d)\n",
			pdata->version, pci_bus);
		return -EINVAL;
	}

	return 0;
}
#endif

static int sdc_get_ctlr_info(struct sdc_board *priv, void __iomem *base,
								int next_offset)
{
	struct sdc_config *config = &priv->config;
#if SDC_CREATE_UART
	struct sdc_uart *uart = priv->uart;
#endif
#if SDC_CREATE_DIO || SDC_CREATE_GPIO
	struct sdc_dio *dio = priv->dio;
#endif
#if SDC_CREATE_SPI
	struct sdc_spi *spi = priv->spi;
#endif
#if SDC_CREATE_CAN
	struct sdc_can *can = priv->can;
#endif
#if SDC_CREATE_PARPORT
	struct sdc_parport *parport = priv->parport;
#endif
	int offset, ret, i, pci_bus = priv->pdev->bus->number;
	u32 temp;
	u8 type;

	priv->nr_ctlr_supported = 0;

	for (i = 0; i < priv->pdata.nr_controller; i++) {
		offset = next_offset;

		temp = sdc_readl(base, offset, 0);
		type = (temp & GENMASK(15, 8)) >> 8;
		temp = sdc_readl(base, offset, 1);
		next_offset = temp & GENMASK(15, 0);

		switch (type) {
		case SDC_DEV_TYPE_CONFIG:
			if (++priv->nr_config > SDC_CONFIG_MAX)
				return -EINVAL;
			ret = sdc_get_config_info(config, pci_bus, base, offset);
			if (ret == 0) {
				memcpy(&config->pdata.board, &priv->pdata, sizeof(priv->pdata));
				priv->nr_ctlr_supported++;
			}
			break;

		case SDC_DEV_TYPE_UART:
#if SDC_CREATE_UART
			if (++priv->nr_uart > SDC_UART_MAX)
				return -EINVAL;
			ret = sdc_get_uart_info(uart, pci_bus, base, offset);
			if (ret == 0) {
				memcpy(&uart->pdata.board, &priv->pdata, sizeof(priv->pdata));
				uart++;
				priv->nr_ctlr_supported++;
			}
#endif
			break;

		case SDC_DEV_TYPE_DIO:
#if SDC_CREATE_DIO || SDC_CREATE_GPIO
			if (++priv->nr_dio > SDC_DIO_MAX)
				return -EINVAL;
			ret = sdc_get_dio_info(dio, pci_bus, base, offset);
			if (ret == 0) {
				memcpy(&dio->pdata.board, &priv->pdata, sizeof(priv->pdata));
				dio++;
				priv->nr_ctlr_supported++;
			}
#endif
			break;

		case SDC_DEV_TYPE_SPI:
#if SDC_CREATE_SPI
			if (++priv->nr_spi > SDC_SPI_MAX)
				return -EINVAL;
			ret = sdc_get_spi_info(spi, pci_bus, base, offset);
			if (ret == 0) {
				memcpy(&spi->pdata.board, &priv->pdata, sizeof(priv->pdata));
				spi++;
				priv->nr_ctlr_supported++;
			}
#endif
			break;

		case SDC_DEV_TYPE_CAN:
#if SDC_CREATE_CAN
			if (++priv->nr_can > SDC_CAN_MAX)
				return -EINVAL;
			ret = sdc_get_can_info(can, pci_bus, base, offset);
			if (ret == 0) {
				memcpy(&can->pdata.board, &priv->pdata, sizeof(priv->pdata));
				can++;
				priv->nr_ctlr_supported++;
			}
#endif
			break;

		case SDC_DEV_TYPE_PARPORT:
#if SDC_CREATE_PARPORT
			if (++priv->nr_parport > SDC_PARPORT_MAX)
				return -EINVAL;
			ret = sdc_get_parport_info(parport, pci_bus, base, offset);
			if (ret == 0) {
				memcpy(&parport->pdata.board, &priv->pdata, sizeof(priv->pdata));
				parport++;
				priv->nr_ctlr_supported++;
			}
#endif
			break;

		default:
			return -EINVAL;
		}
	}

	return 0;
}

static void sdc_setup_cells(struct sdc_board *priv, struct mfd_cell *cells)
{
	struct resource *res;
	struct pci_dev *pci;
	int i;

	pci = priv->pdev;
	i = 0;

	if (priv->nr_config) {
		if (priv->config.supported) {
			res = priv->config.res;

			res->start = pci->resource[2].start;
			res->end = res->start + priv->config.pdata.mem_offset - 1;
			res->flags = IORESOURCE_MEM;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			res++;
			res->start = pci->resource[2].start + priv->config.pdata.mem_offset;
			res->end = res->start + priv->config.pdata.mem_size - 1;
			res->flags = IORESOURCE_MEM;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			res++;
			res->start = 0;
			res->end =  0;
			res->flags = IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			cells->name = "sdc_config";
			cells->num_resources = ARRAY_SIZE(priv->config.res);
			cells->resources = priv->config.res;
			cells->platform_data = &priv->config.pdata;
			cells->pdata_size  = sizeof(priv->config.pdata);
			cells++;
		}
	}

#if SDC_CREATE_UART
	for (i = 0; i < priv->nr_uart; i++) {
		if (priv->uart[i].supported) {
			res = priv->uart[i].res;

			res->start = pci->resource[1].start + priv->uart[i].pdata.io_offset;
			res->end = res->start + priv->uart[i].pdata.io_size - 1;
			res->flags = IORESOURCE_IO;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			res++;
			res->start = 0;
			res->end =  0;
			res->flags = IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			cells->name = "8250_sdc";
			cells->num_resources = ARRAY_SIZE(priv->uart[i].res);
			cells->resources = priv->uart[i].res;
			cells->platform_data = &priv->uart[i].pdata;
			cells->pdata_size  = sizeof(priv->uart[i].pdata);
			cells++;
		}
	}
#endif

#if SDC_CREATE_DIO || SDC_CREATE_GPIO
	for (i = 0; i < priv->nr_dio; i++) {
		if (priv->dio[i].supported) {
			res = priv->dio[i].res;

			res->start = pci->resource[2].start + priv->dio[i].pdata.mem_offset;
			res->end = res->start + priv->dio[i].pdata.mem_size - 1;
			res->flags = IORESOURCE_MEM;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			res++;
			res->start = pci->resource[0].start + 32 +
								(priv->dio[i].pdata.index * 4);
			res->end = res->start + 3;
			res->flags = IORESOURCE_MEM;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			res++;
			res->start = 0;
			res->end =  0;
			res->flags = IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

#if SDC_CREATE_DIO
			cells->name = "sdc_dio";
#else
			cells->name = "sdc_gpio";
#endif
			cells->num_resources = ARRAY_SIZE(priv->dio[i].res);
			cells->resources = priv->dio[i].res;
			cells->platform_data = &priv->dio[i].pdata;
			cells->pdata_size  = sizeof(priv->dio[i].pdata);
			cells++;
		}
	}
#endif

#if SDC_CREATE_SPI
	for (i = 0; i < priv->nr_spi; i++) {
		if (priv->spi[i].supported) {
			res = priv->spi[i].res;

			res->start = pci->resource[2].start + priv->spi[i].pdata.mem_offset;
			res->end = res->start + priv->spi[i].pdata.mem_size - 1;
			res->flags = IORESOURCE_MEM;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			res++;
			res->start = pci->resource[0].start + 32 +
								(priv->spi[i].pdata.index * 4);
			res->end = res->start + 3;
			res->flags = IORESOURCE_MEM;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			res++;
			res->start = 0;
			res->end =  0;
			res->flags = IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			cells->name = "spi_sdc";
			cells->num_resources = ARRAY_SIZE(priv->spi[i].res);
			cells->resources = priv->spi[i].res;
			cells->platform_data = &priv->spi[i].pdata;
			cells->pdata_size  = sizeof(priv->spi[i].pdata);
			cells++;
		}
	}
#endif

#if SDC_CREATE_CAN
	for (i = 0; i < priv->nr_can; i++) {
		if (priv->can[i].supported) {
			res = priv->can[i].res;

			res->start = pci->resource[2].start + priv->can[i].pdata.mem_offset;
			res->end = res->start + priv->can[i].pdata.mem_size - 1;
			res->flags = IORESOURCE_MEM;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			res++;
			res->start = pci->resource[0].start + 32 +
								(priv->can[i].pdata.index * 4);
			res->end = res->start + 3;
			res->flags = IORESOURCE_MEM;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			res++;
			res->start = 0;
			res->end =  0;
			res->flags = IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			cells->name = "can_sdc";
			cells->num_resources = ARRAY_SIZE(priv->can[i].res);
			cells->resources = priv->can[i].res;
			cells->platform_data = &priv->can[i].pdata;
			cells->pdata_size  = sizeof(priv->can[i].pdata);
			cells++;
		}
	}
#endif

#if SDC_CREATE_PARPORT
	for (i = 0; i < priv->nr_parport; i++) {
		if (priv->parport[i].supported) {
			res = priv->parport[i].res;

			res->start = pci->resource[2].start + priv->parport[i].pdata.mem_offset;
			res->end = res->start + priv->parport[i].pdata.mem_size - 1;
			res->flags = IORESOURCE_MEM;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			res++;
			res->start = pci->resource[0].start + 32 +
								(priv->parport[i].pdata.index * 4);
			res->end = res->start + 3;
			res->flags = IORESOURCE_MEM;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			res++;
			res->start = 0;
			res->end =  0;
			res->flags = IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE;
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
			res->desc = IORES_DESC_NONE;
#endif

			cells->name = "parport_sdc";
			cells->num_resources = ARRAY_SIZE(priv->parport[i].res);
			cells->resources = priv->parport[i].res;
			cells->platform_data = &priv->parport[i].pdata;
			cells->pdata_size  = sizeof(priv->parport[i].pdata);
			cells++;
		}
	}
#endif
}

static int sdc_board_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct sdc_board *priv;
	struct mfd_cell *cells;
	void __iomem *base;
	int next_offset;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	pci_set_drvdata(pdev, priv);
	priv->pdev = pdev;

	ret = pcim_enable_device(pdev);
	if (ret < 0)
		return ret;

	pci_set_master(pdev);

	base = pcim_iomap(pdev, 2, pci_resource_len(pdev, 2));
	if (!base)
		return -ENOMEM;

	ret = sdc_get_basic_info(priv, base, &next_offset);
	if (ret < 0)
		return ret;

	ret = sdc_get_ctlr_info(priv, base, next_offset);
	if (ret < 0)
		return ret;

	if (!priv->nr_ctlr_supported)
		return -ENODEV;

	cells = devm_kcalloc(&pdev->dev, priv->nr_ctlr_supported,
								sizeof(struct mfd_cell), GFP_KERNEL);
	if (!cells)
		return -ENOMEM;

	sdc_setup_cells(priv, cells);

	ret = mfd_add_devices(&pdev->dev, PLATFORM_DEVID_AUTO, cells,
					priv->nr_ctlr_supported, NULL, pdev->irq, NULL);
	if (ret)
		return ret;

	return 0;
}

static void sdc_board_remove(struct pci_dev *pdev)
{
	mfd_remove_devices(&pdev->dev);
}

static const struct pci_device_id sdc_board_pci_ids[] = {
#if KERNEL_VERSION(4, 17, 0) <= LINUX_VERSION_CODE
	{ PCI_VDEVICE(SUNIX, 0x2000) },
#else
	{ 0x1fd4, 0x2000, 0x1fd4, 0x0001, 0, 0, 123 },
#endif
	{ }
};
MODULE_DEVICE_TABLE(pci, sdc_board_pci_ids);

static struct pci_driver sdc_board_driver = {
	.name = DRV_NAME,
	.id_table = sdc_board_pci_ids,
	.probe = sdc_board_probe,
	.remove = sdc_board_remove,
};

#ifdef CONFIG_PROC_FS
static int sdc_version_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s", SDC_DRIVER_VERSION);
	return 0;
}

#if KERNEL_VERSION(4, 18, 0) > LINUX_VERSION_CODE
static int sdc_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, sdc_version_proc_show, NULL);
}

static const struct file_operations sdc_proc_fops = {
	.open		= sdc_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static struct proc_dir_entry *driver_ent;
static struct proc_dir_entry *version_ent;
#endif

static int __init sdc_board_init(void)
{
	int ret;

#ifdef CONFIG_PROC_FS
	driver_ent = proc_mkdir(DRV_NAME, NULL);
	if (driver_ent) {
#if KERNEL_VERSION(4, 18, 0) <= LINUX_VERSION_CODE
		version_ent = proc_create_single_data("version", 0444, driver_ent,
											sdc_version_proc_show, NULL);
#else
		version_ent = proc_create("version", 0444, driver_ent,
											&sdc_proc_fops);
#endif
		if (!version_ent)
			pr_warn("failed to create version proc entry\n");
	} else {
		pr_warn("failed to create driver proc entry\n");
	}
#endif

	ret = pci_register_driver(&sdc_board_driver);
	if (ret < 0)
		goto out_remove_proc_entry;

	return 0;

out_remove_proc_entry:
#ifdef CONFIG_PROC_FS
	if (driver_ent) {
		if (version_ent)
			remove_proc_entry("version", driver_ent);
		remove_proc_entry(DRV_NAME, NULL);
	}
#endif
	return ret;
}
module_init(sdc_board_init);

static void __exit sdc_board_exit(void)
{
	pci_unregister_driver(&sdc_board_driver);

#ifdef CONFIG_PROC_FS
	if (driver_ent) {
		if (version_ent)
			remove_proc_entry("version", driver_ent);
		remove_proc_entry(DRV_NAME, NULL);
	}
#endif
}
module_exit(sdc_board_exit);

MODULE_AUTHOR("Jason Lee <jason_lee@sunix.com>");
MODULE_DESCRIPTION("SUNIX SDC PCIe board core driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);

// SPDX-License-Identifier: GPL-2.0
/*
 * spi-sdc - SUNIX SDC SPI controller driver
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
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/spi/spi.h>
#include <linux/crc16.h>
#include "sunix-sdc.h"

#define ENABLE_CREATE_MCP251X			1
#define ENABLE_CREATE_MCP251XFD			1

#define DRV_NAME				"spi_sdc"

#define MODALIAS_MCP251X		"sdc_mcp251x"
#define MODALIAS_MCP251XFD		"sdc_mcp251xfd"

#define CTRL_REG				0x00
#define STATUS_REG				0x04
#define PORT_IRQ_ENABLE_REG		0x0C
#define GPIO_OUTPUT_ENABLE_REG	0x14
#define GPIO_OUTPUT_REG			0x18
#define TRANS0_REG				0x1C
#define TRANS1_REG				0x20
#define RAM_REG					0x200
/* SPI RAM 512 byte */

/* Control register bit fields */
#define CTRL_SPI_EN				BIT(0)
#define CTRL_TX_IRQ_EN			BIT(1)
#define CTRL_RX_IRQ_EN			BIT(2)
#define CTRL_DEVICE_IRQ_EN		BIT(3)
#define CTRL_DIVISOR_MASK		GENMASK(31, 16)

/* Status register bit fields */
#define STATUS_BUSY				BIT(0)
#define STATUS_TX_IRQ			BIT(1)
#define STATUS_RX_IRQ			BIT(2)
#define STATUS_DEVICE_IRQ		BIT(3)

/* Transaction register 0 bit fields */
#define TRANS_ENABLE			BIT(0)
#define TRANS_WRITE				BIT(1)

/* Event header bit fields */
#define EVENT_BUSY				BIT(0)
#define EVENT_TX_IRQ			BIT(1)
#define EVENT_RX_IRQ			BIT(2)
#define EVENT_DEVICE_IRQ		BIT(3)

#define MCP251X_SPEED_HZ_MAX	10000000
#define MCP251X_OSC				20000000
#define MCP251XFD_SPEED_HZ_MAX	20000000
#define MCP251XFD_OSC			40000000

struct sdc_spi_device {
	struct sdc_spi_device_platdata *pdata;
	struct spi_board_info board;
	struct clk *clk;
	struct clk_lookup *clk_lookup;
	struct spi_device *sdev;
	int created;
};

struct sdc_spi {
	struct sdc_spi_platdata *pdata;
	struct platform_device *pdev;
	int irq;
	void __iomem *base;
	void __iomem *event;
	struct sdc_spi_device device;
	struct spi_controller *ctlr;
	spinlock_t lock;
	u8 mcp251xfd_last_cmd[3];

#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *dev_ent;
#endif

	u32 ctrl_pm;
	u32 port_irq_pm;
	u32 gpio_output_pm;
};

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *driver_ent;
#endif

static int sdc_spi_clk_div(struct sdc_spi *priv, unsigned int freq)
{
	int div = DIV_ROUND_UP(priv->pdata->clk_rate, freq * 2);

	if (div < 0)
		return 0;
	else
		return div;
}

static int sdc_spi_setup(struct spi_device *spi)
{
	struct sdc_spi *priv = spi_controller_get_devdata(spi->controller);

	if ((spi->max_speed_hz == 0) ||
		(spi->max_speed_hz > (priv->pdata->clk_rate / 2))) {
		spi->max_speed_hz = priv->pdata->clk_rate / 2;

	} else if (spi->max_speed_hz < (priv->pdata->clk_rate / 256)) {
		dev_err(&spi->dev, "spi clock is too low\n");
		return -EINVAL;
	}

	return 0;
}

static void sdc_spi_set_ram(void __iomem *base, u8 *buf, int len)
{
	int done, idx, cur, i;
	u32 reg;
	u8 *tmp;

	/* set SPI RAM */
	idx = 0;
	for (done = 0; done < len; done += 4) {
		cur = len - done;
		if (cur > 4)
			cur = 4;
		tmp = buf + done;
		reg = 0;
		for (i = 0; i < cur; i++)
			reg |= tmp[i] << (i * 8);

		writel(reg, base + RAM_REG + (idx * 4));
		idx++;
	}
}

static void sdc_spi_get_ram(void __iomem *base, u8 *buf, int len)
{
	int done, idx, cur, i;
	u32 reg;
	u8 *tmp;

	idx = 0;
	for (done = 0; done < len; done += 4) {
		reg = readl(base + RAM_REG + (idx * 4));
		idx++;
		cur = len - done;
		if (cur > 4)
			cur = 4;
		tmp = buf + done;
		for (i = 0; i < cur; i++)
			tmp[i] = (reg & 0xff << (i * 8)) >> (i * 8);
	}
}

#define SDC_SPI_TRANS_WRITE		0x01
#define SDC_SPI_TRANS_READ		0x02

static int sdc_spi_mcp251x_trans(struct sdc_spi *priv, struct spi_device *spi,
								struct spi_transfer *t, u8 *trans, u16 *cmd_len,
								u16 *data_len, int *do_trans)
{
	struct sdc_spi_device_platdata *pdata = spi->controller_data;
	u8 *buf;
	u32 reg;
	int ret = 0;

	*do_trans = 0;

	if (t->tx_buf) {
		buf = (u8 *)t->tx_buf;
		switch (buf[0]) {
		case 0x02:
			*trans = SDC_SPI_TRANS_WRITE;
			*cmd_len = 2;
			*data_len = t->len - *cmd_len;
			*do_trans = 1;
			break;
		case 0x03:
			*trans = SDC_SPI_TRANS_READ;
			*cmd_len = 2;
			*data_len = t->len - *cmd_len;
			*do_trans = 1;
			break;
		case 0x05:
			*trans = SDC_SPI_TRANS_WRITE;
			*cmd_len = 2;
			*data_len = t->len - *cmd_len;
			*do_trans = 1;
			break;
		case 0x40:
		case 0x42:
		case 0x44:
			*trans = SDC_SPI_TRANS_WRITE;
			*cmd_len = 1;
			*data_len = t->len - *cmd_len;
			*do_trans = 1;
			break;
		case 0x81:
		case 0x82:
		case 0x84:
			*trans = SDC_SPI_TRANS_WRITE;
			*cmd_len = t->len;
			*data_len = 0;
			*do_trans = 1;
			break;
		case 0x90:
		case 0x94:
			*trans = SDC_SPI_TRANS_READ;
			*cmd_len = 1;
			*data_len = t->len - *cmd_len;
			*do_trans = 1;
			break;
		case 0xc0:
			*trans = SDC_SPI_TRANS_WRITE;
			*cmd_len = t->len;
			*data_len = 0;
			*do_trans = 1;
			break;
		case 0xf0:
			/* Set termination */
			*do_trans = 0;
			reg = readl(priv->base + GPIO_OUTPUT_REG);
			reg &= ~pdata->can_termination;
			reg |= buf[1] ? pdata->can_termination : 0;
			writel(pdata->can_termination, priv->base + GPIO_OUTPUT_ENABLE_REG);
			writel(reg, priv->base + GPIO_OUTPUT_REG);
			break;
		default:
			ret = -EINVAL;
			break;
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int sdc_spi_mcp251xfd_trans(struct sdc_spi *priv, struct spi_device *spi,
								struct spi_transfer *t, u8 *trans, u16 *cmd_len,
								u16 *data_len, int *do_trans)
{
	struct sdc_spi_device_platdata *pdata = spi->controller_data;
	u8 *buf;
	u32 reg;
	int ret = 0;

	*do_trans = 0;

	if (t->tx_buf) {
		buf = (u8 *)t->tx_buf;
		switch (buf[0] & 0xf0) {
		case 0x00:
			*trans = SDC_SPI_TRANS_WRITE;
			*cmd_len = 2;
			*data_len = 0;
			*do_trans = 1;
			priv->mcp251xfd_last_cmd[0] = buf[0];
			priv->mcp251xfd_last_cmd[1] = buf[1];
			break;
		case 0x20:
			*trans = SDC_SPI_TRANS_WRITE;
			*cmd_len = 2;
			*data_len = t->len - *cmd_len;
			*do_trans = 1;
			priv->mcp251xfd_last_cmd[0] = buf[0];
			priv->mcp251xfd_last_cmd[1] = buf[1];
			break;
		case 0x30:
			*trans = SDC_SPI_TRANS_READ;
			*cmd_len = 2;
			*data_len = t->len - *cmd_len;
			*do_trans = 1;
			priv->mcp251xfd_last_cmd[0] = buf[0];
			priv->mcp251xfd_last_cmd[1] = buf[1];
			break;
		case 0xa0:
			*trans = SDC_SPI_TRANS_WRITE;
			*cmd_len = 3;
			*data_len = t->len - *cmd_len;
			*do_trans = 1;
			priv->mcp251xfd_last_cmd[0] = buf[0];
			priv->mcp251xfd_last_cmd[1] = buf[1];
			priv->mcp251xfd_last_cmd[2] = buf[2];
			break;
		case 0xb0:
			*trans = SDC_SPI_TRANS_READ;
			*cmd_len = 3;
			*data_len = t->len - *cmd_len;
			*do_trans = 1;
			priv->mcp251xfd_last_cmd[0] = buf[0];
			priv->mcp251xfd_last_cmd[1] = buf[1];
			priv->mcp251xfd_last_cmd[2] = buf[2];
			break;
		case 0xf0:
			/* Set termination */
			*do_trans = 0;
			reg = readl(priv->base + GPIO_OUTPUT_REG);
			reg &= ~pdata->can_termination;
			reg |= buf[1] ? pdata->can_termination : 0;
			writel(pdata->can_termination, priv->base + GPIO_OUTPUT_ENABLE_REG);
			writel(reg, priv->base + GPIO_OUTPUT_REG);
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}

	if (!t->tx_buf && t->rx_buf) {
		switch (priv->mcp251xfd_last_cmd[0] & 0xf0) {
		case 0x30:
			*trans = SDC_SPI_TRANS_READ;
			*cmd_len = 2;
			*data_len = t->len - *cmd_len;
			*do_trans = 1;
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}

	return ret;
}

static int sdc_spi_transfer_one_message(struct spi_controller *ctlr,
								struct spi_message *m)
{
	struct sdc_spi *priv = spi_controller_get_devdata(ctlr);
	struct spi_transfer *t = NULL;
	struct spi_device *spi = m->spi;
	struct sdc_spi_device_platdata *pdata = spi->controller_data;
	unsigned long flags;
	u16 cmd_len = 0, data_len = 0;
	int ret, stat, div, do_trans;
	u32 reg, cs;
	u8 trans = SDC_SPI_TRANS_WRITE;

	m->actual_length = 0;

	spin_lock_irqsave(&priv->lock, flags);

	if (!pdata) {
		stat = -EINVAL;
		goto msg_done;
	}

	stat = readl_poll_timeout(priv->base + STATUS_REG, reg,
				!(reg & STATUS_BUSY), 0, 100);
	if (stat < 0)
		goto msg_done;

	list_for_each_entry(t, &m->transfers, transfer_list) {

		if (memcmp(pdata->name, "MCP25625", 8) == 0) {
			ret = sdc_spi_mcp251x_trans(priv, spi, t, &trans, &cmd_len,
												&data_len, &do_trans);
			if (ret < 0) {
				stat = -EINVAL;
				goto msg_done;
			}
			if (!do_trans)
				goto msg_pass;

		} else if (memcmp(pdata->name, "MCP2518FD", 9) == 0) {
			ret = sdc_spi_mcp251xfd_trans(priv, spi, t, &trans, &cmd_len,
												&data_len, &do_trans);
			if (ret < 0) {
				stat = -EINVAL;
				goto msg_done;
			}
			if (!do_trans)
				goto msg_pass;

		} else {
			stat = -EINVAL;
			goto msg_done;
		}

		if (t->speed_hz)
			div = sdc_spi_clk_div(priv, t->speed_hz);
		else
			div = sdc_spi_clk_div(priv, spi->max_speed_hz);
		if (div < 0) {
			stat = -EIO;
			goto msg_done;
		}

		/* Set divisor */
		reg = readl(priv->base + CTRL_REG);
		reg &= ~CTRL_DIVISOR_MASK;
		reg |= div << 16;
		writel(reg, priv->base + CTRL_REG);

		/* Set command and data to ram register */
		if (trans == SDC_SPI_TRANS_READ) {
			if (t->tx_buf) {
				sdc_spi_set_ram(priv->base, (u8 *)t->tx_buf, cmd_len);
			} else {
				if (memcmp(pdata->name, "MCP2518FD", 9) == 0) {
					sdc_spi_set_ram(priv->base,
						priv->mcp251xfd_last_cmd, cmd_len);
				} else {
					stat = -EINVAL;
					goto msg_done;
				}
			}
		} else {
			if (t->tx_buf) {
				sdc_spi_set_ram(priv->base, (u8 *)t->tx_buf, t->len);
			} else {
				stat = -EINVAL;
				goto msg_done;
			}
		}

		/* Set transaction 1 register */
		if (trans == SDC_SPI_TRANS_READ)
			reg = data_len;
		else
			reg = data_len << 16;
		writel(reg, priv->base + TRANS1_REG);

		/* Set transaction 0 register */
		if (trans == SDC_SPI_TRANS_READ)
			reg = TRANS_ENABLE;
		else
			reg = TRANS_ENABLE | TRANS_WRITE;
		reg |= cmd_len << 8;
#if KERNEL_VERSION(6, 8, 0) <= LINUX_VERSION_CODE
		cs = (u32)spi->chip_select[0];
#else
		cs = (u32)spi->chip_select;
#endif
		reg |= cs << 16;
		writel(reg, priv->base + TRANS0_REG);

		/* Wait process finish */
		stat = readl_poll_timeout(priv->base + STATUS_REG, reg,
					!(reg & STATUS_BUSY), 0, 2000);
		if (stat < 0)
			goto msg_done;

		/* Fetch data */
		if (trans == SDC_SPI_TRANS_READ && data_len > 0 && t->rx_buf)
			sdc_spi_get_ram(priv->base, (u8 *)t->rx_buf + cmd_len, data_len);

msg_pass:
		m->actual_length = t->len;
	}

msg_done:
	m->status = stat;
	spi_finalize_current_message(ctlr);

	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;
}

static irqreturn_t sdc_spi_irq_handler(int irq, void *dev_id)
{
	struct sdc_spi *priv = dev_id;
	u32 event;

	event = readl(priv->event);

	if (event & EVENT_DEVICE_IRQ) {
		/* CAN device irq handler handle request */
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static void sdc_spi_prepare_device(struct sdc_spi *priv,
								struct sdc_spi_device *ssdev)
{
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&priv->lock, flags);

	/* Set termination */
	reg = readl(priv->base + GPIO_OUTPUT_REG);
	reg |= ssdev->pdata->can_termination;
	writel(ssdev->pdata->can_termination, priv->base + GPIO_OUTPUT_ENABLE_REG);
	writel(reg, priv->base + GPIO_OUTPUT_REG);

	/* Enable port irq */
	reg = readl(priv->base + PORT_IRQ_ENABLE_REG);
	reg |= ssdev->pdata->can_irq;
	writel(reg, priv->base + PORT_IRQ_ENABLE_REG);

	spin_unlock_irqrestore(&priv->lock, flags);
}

static void sdc_spi_unprepare_device(struct sdc_spi *priv,
								struct sdc_spi_device *ssdev)
{
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&priv->lock, flags);

	/* Disable port irq */
	reg = readl(priv->base + PORT_IRQ_ENABLE_REG);
	reg &= ~ssdev->pdata->can_irq;
	writel(reg, priv->base + PORT_IRQ_ENABLE_REG);

	/* Clear termination */
	reg = readl(priv->base + GPIO_OUTPUT_REG);
	reg &= ~ssdev->pdata->can_termination;
	writel(ssdev->pdata->can_termination, priv->base + GPIO_OUTPUT_ENABLE_REG);
	writel(reg, priv->base + GPIO_OUTPUT_REG);

	spin_unlock_irqrestore(&priv->lock, flags);
}

#if ENABLE_CREATE_MCP251X
static int sdc_spi_create_mcp251x(struct sdc_spi *priv,
								struct sdc_spi_device *ssdev)
{
	struct spi_board_info *info = &ssdev->board;
	char name[64];
	int ret;

	sdc_spi_prepare_device(priv, ssdev);

	strscpy(info->modalias, MODALIAS_MCP251X, strlen(MODALIAS_MCP251X) + 1);
	info->irq = priv->irq;
	info->max_speed_hz = MCP251X_SPEED_HZ_MAX;
	info->chip_select = ssdev->pdata->cs;
	/* Pass platform info to spi device */
	info->controller_data = ssdev->pdata;

	snprintf(name, sizeof(name), "spi_sdc.%d.%d", priv->ctlr->bus_num,
													info->chip_select);
	/* Register clk for mcp251x */
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
	ssdev->clk =
		clk_register_fixed_rate(NULL, name, NULL, 0, MCP251X_OSC);
#else
	ssdev->clk =
		clk_register_fixed_rate(NULL, name, NULL, CLK_IS_ROOT, MCP251X_OSC);
#endif
	if (IS_ERR(ssdev->clk)) {
		ret = PTR_ERR(ssdev->clk);
		goto out_unprepare_device;
	}

#if KERNEL_VERSION(4, 2, 0) <= LINUX_VERSION_CODE
	ssdev->clk_lookup = clkdev_create(ssdev->clk, NULL, "spi%d.%d",
							priv->ctlr->bus_num, info->chip_select);
	if (!ssdev->clk_lookup) {
		ret = -ENOMEM;
		goto out_unregister_clk;
	}
#else
	ssdev->clk_lookup = clkdev_alloc(ssdev->clk, NULL, "spi%d.%d",
							priv->ctlr->bus_num, info->chip_select);
	if (!ssdev->clk_lookup) {
		ret = -ENOMEM;
		goto out_unregister_clk;
	}

	clkdev_add(ssdev->clk_lookup);
#endif
	ssdev->sdev = spi_new_device(priv->ctlr, info);
	if (!ssdev->sdev) {
		ret = -ENODEV;
		goto out_drop_clkdev;
	}

	ssdev->created = 1;
	return 0;

out_drop_clkdev:
	clkdev_drop(ssdev->clk_lookup);
out_unregister_clk:
	clk_unregister(ssdev->clk);
out_unprepare_device:
	sdc_spi_unprepare_device(priv, ssdev);
	return ret;
}

static void sdc_spi_remove_mcp251x(struct sdc_spi *priv,
								struct sdc_spi_device *ssdev)
{
	/* Unregister clk */
	clkdev_drop(ssdev->clk_lookup);
	clk_unregister(ssdev->clk);

	sdc_spi_unprepare_device(priv, ssdev);
	ssdev->created = 0;
}
#endif

#if ENABLE_CREATE_MCP251XFD
static int sdc_spi_create_mcp251xfd(struct sdc_spi *priv,
								struct sdc_spi_device *ssdev)
{
	struct spi_board_info *info = &ssdev->board;
	char name[64];
	int ret;

	sdc_spi_prepare_device(priv, ssdev);

	strscpy(info->modalias, MODALIAS_MCP251XFD, strlen(MODALIAS_MCP251XFD) + 1);
	info->irq = priv->irq;
	info->max_speed_hz = MCP251XFD_SPEED_HZ_MAX;
	info->chip_select = ssdev->pdata->cs;
	/* Pass platform info to spi device */
	info->controller_data = ssdev->pdata;

	snprintf(name, sizeof(name), "spi_sdc.%d.%d", priv->ctlr->bus_num,
													info->chip_select);
	/* Register clk for mcp251xfd */
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
	ssdev->clk =
		clk_register_fixed_rate(NULL, name, NULL, 0, MCP251XFD_OSC);
#else
	ssdev->clk =
		clk_register_fixed_rate(NULL, name, NULL, CLK_IS_ROOT, MCP251XFD_OSC);
#endif
	if (IS_ERR(ssdev->clk)) {
		ret = PTR_ERR(ssdev->clk);
		goto out_unprepare_device;
	}

#if KERNEL_VERSION(4, 2, 0) <= LINUX_VERSION_CODE
	ssdev->clk_lookup = clkdev_create(ssdev->clk, NULL, "spi%d.%d",
							priv->ctlr->bus_num, info->chip_select);
	if (!ssdev->clk_lookup) {
		ret = -ENOMEM;
		goto out_unregister_clk;
	}
#else
	ssdev->clk_lookup = clkdev_alloc(ssdev->clk, NULL, "spi%d.%d",
							priv->ctlr->bus_num, info->chip_select);
	if (!ssdev->clk_lookup) {
		ret = -ENOMEM;
		goto out_unregister_clk;
	}

	clkdev_add(ssdev->clk_lookup);
#endif
	ssdev->sdev = spi_new_device(priv->ctlr, info);
	if (!ssdev->sdev) {
		ret = -ENODEV;
		goto out_drop_clkdev;
	}

	ssdev->created = 1;
	return 0;

out_drop_clkdev:
	clkdev_drop(ssdev->clk_lookup);
out_unregister_clk:
	clk_unregister(ssdev->clk);
out_unprepare_device:
	sdc_spi_unprepare_device(priv, ssdev);
	return ret;
}

static void sdc_spi_remove_mcp251xfd(struct sdc_spi *priv,
								struct sdc_spi_device *ssdev)
{
	/* Unregister clk */
	clkdev_drop(ssdev->clk_lookup);
	clk_unregister(ssdev->clk);

	sdc_spi_unprepare_device(priv, ssdev);
	ssdev->created = 0;
}
#endif

static int sdc_spi_create_device(struct sdc_spi *priv)
{
	struct sdc_spi_device *device = NULL;
	int ret = 0;

	device = &priv->device;

	if (!device->pdata) {
		ret = -ENODEV;
		goto out;
	}

#if ENABLE_CREATE_MCP251X
	if (device->pdata->type == SDC_SPI_DEVICE_CAN &&
		memcmp(device->pdata->name, "MCP25625", 8) == 0) {
		ret = sdc_spi_create_mcp251x(priv, device);
		if (ret < 0)
			goto out;
	}
#endif

#if ENABLE_CREATE_MCP251XFD
	if (device->pdata->type == SDC_SPI_DEVICE_CAN &&
		memcmp(device->pdata->name, "MCP2518FD", 9) == 0) {
		ret = sdc_spi_create_mcp251xfd(priv, device);
		if (ret < 0)
			goto out;
	}
#endif

out:
	return ret;
}

static void sdc_spi_remove_device(struct sdc_spi *priv)
{
	struct sdc_spi_device *device = NULL;

	device = &priv->device;

	if (!device->pdata)
		return;

#if ENABLE_CREATE_MCP251X
	if (device->pdata->type == SDC_SPI_DEVICE_CAN &&
		memcmp(device->pdata->name, "MCP25625", 8) == 0)
		sdc_spi_remove_mcp251x(priv, device);
#endif

#if ENABLE_CREATE_MCP251XFD
	if (device->pdata->type == SDC_SPI_DEVICE_CAN &&
		memcmp(device->pdata->name, "MCP2518FD", 9) == 0)
		sdc_spi_remove_mcp251xfd(priv, device);
#endif
}

static void sdc_spi_hw_enable(struct sdc_spi *priv)
{
	/* Enable spi and device irq */
	writel(CTRL_SPI_EN | CTRL_DEVICE_IRQ_EN, priv->base + CTRL_REG);
}

static void sdc_spi_hw_disable(struct sdc_spi *priv)
{
	/* Disable spi and device irq */
	writel(0, priv->base + CTRL_REG);
}

#ifdef CONFIG_PROC_FS
static int sdc_spi_proc_show(struct seq_file *m, void *v)
{
	struct sdc_spi *priv = m->private;

	seq_printf(m, "pci_bus=%d;irq=%d;index=%d;pid=%d;nr_device=%d;",
		priv->pdata->board.pci_bus, priv->pdata->board.irq, priv->pdata->index,
		priv->pdev->id, priv->pdata->nr_device);

	seq_printf(m, "spi_bus_%d=%d;cs_%d=%d;",
		0, priv->pdev->id, 0, priv->device.pdata->cs);

	seq_putc(m, '\n');
	return 0;
}

#if KERNEL_VERSION(4, 18, 0) > LINUX_VERSION_CODE
static int sdc_spi_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, sdc_spi_proc_show, PDE_DATA(inode));
}

static const struct file_operations sdc_spi_proc_fops = {
	.open		= sdc_spi_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static int sdc_spi_proc_create(struct sdc_spi *priv)
{
	char buf[16] = {0};

	if (!driver_ent)
		return -ENOMEM;

	sprintf(buf, "spi_bus%d", priv->pdev->id);
#if KERNEL_VERSION(4, 18, 0) <= LINUX_VERSION_CODE
	priv->dev_ent =	proc_create_single_data(buf, 0444, driver_ent,
											sdc_spi_proc_show, priv);
#else
	priv->dev_ent = proc_create_data(buf, 0444, driver_ent,
											&sdc_spi_proc_fops, priv);
#endif
	if (!priv->dev_ent)
		return -ENOMEM;

	return 0;
}

static void sdc_spi_proc_remove(struct sdc_spi *priv)
{
	char buf[16] = {0};

	if (priv->dev_ent) {
		sprintf(buf, "spi_bus%d", priv->pdev->id);
		remove_proc_entry(buf, driver_ent);
	}
}
#endif

static int sdc_spi_probe(struct platform_device *pdev)
{
	struct sdc_spi_platdata *pdata;
	struct spi_controller *ctlr;
	struct resource *res;
	struct sdc_spi *priv;
	int ret;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata)
		return -ENODEV;

	if (pdata->nr_device != 1) {
		dev_err(&pdev->dev, "number of device not 1, nr_device %d\n", pdata->nr_device);
		return -ENODEV;
	}

	ctlr = __spi_alloc_controller(&pdev->dev, sizeof(*priv), false);
	if (!ctlr)
		return -ENOMEM;

	priv = spi_controller_get_devdata(ctlr);
	priv->pdata = pdata;
	priv->pdev = pdev;
	priv->ctlr = ctlr;
	spin_lock_init(&priv->lock);

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		ret = priv->irq;
		goto out_put;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base)) {
		ret = PTR_ERR(priv->base);
		goto out_put;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	priv->event = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->event)) {
		ret = PTR_ERR(priv->event);
		goto out_put;
	}

	priv->device.pdata = &priv->pdata->devices[0];

	ret = request_threaded_irq(priv->irq, sdc_spi_irq_handler, NULL,
							IRQF_SHARED, dev_name(&pdev->dev), priv);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to attach interrupt, err %d\n", ret);
		goto out_put;
	}

	sdc_spi_hw_enable(priv);

	ctlr->bus_num        = pdev->id;
	ctlr->dev.of_node    = pdev->dev.of_node;
	ctlr->num_chipselect = SDC_SPI_DEVICE_MAX;
	ctlr->max_speed_hz   = pdata->clk_rate / 2;
	ctlr->mode_bits      = 0;
	ctlr->flags		   = 0;
	ctlr->bits_per_word_mask = SPI_BPW_MASK(8);
	ctlr->setup = sdc_spi_setup;
	ctlr->transfer_one_message  = sdc_spi_transfer_one_message;

	dev_set_drvdata(&pdev->dev, ctlr);

	ret = spi_register_controller(ctlr);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register spi ctlr, err %d\n", ret);
		goto out_free_irq;
	}

	ret = sdc_spi_create_device(priv);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to create spi device, err %d\n", ret);
		goto out_unregister;
	}

#ifdef CONFIG_PROC_FS
	ret = sdc_spi_proc_create(priv);
	if (ret < 0)
		dev_warn(&pdev->dev, "failed to create device proc entry\n");
#endif

	return 0;

out_unregister:
	sdc_spi_remove_device(priv);
	spi_unregister_controller(ctlr);
out_free_irq:
	sdc_spi_hw_disable(priv);
	free_irq(priv->irq, priv);
out_put:
	spi_controller_put(ctlr);
	return ret;
}

#if KERNEL_VERSION(6, 11, 0) <= LINUX_VERSION_CODE
static void sdc_spi_remove(struct platform_device *pdev)
#else
static int sdc_spi_remove(struct platform_device *pdev)
#endif
{
	struct spi_controller *ctlr = dev_get_drvdata(&pdev->dev);
	struct sdc_spi *priv = spi_controller_get_devdata(ctlr);

#ifdef CONFIG_PROC_FS
	sdc_spi_proc_remove(priv);
#endif

	sdc_spi_remove_device(priv);
	spi_unregister_controller(ctlr);
	sdc_spi_hw_disable(priv);
	free_irq(priv->irq, priv);

#if KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE
	return 0;
#endif
}

#ifdef CONFIG_PM_SLEEP
static int sdc_spi_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct sdc_spi *priv = spi_controller_get_devdata(ctlr);

	priv->ctrl_pm = readl(priv->base + CTRL_REG);
	priv->gpio_output_pm = readl(priv->base + GPIO_OUTPUT_REG);
	priv->port_irq_pm = readl(priv->base + PORT_IRQ_ENABLE_REG);
	return 0;
}

static int sdc_spi_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct sdc_spi *priv = spi_controller_get_devdata(ctlr);

	writel(priv->ctrl_pm, priv->base + CTRL_REG);
	writel(priv->gpio_output_pm, priv->base + GPIO_OUTPUT_ENABLE_REG);
	writel(priv->gpio_output_pm, priv->base + GPIO_OUTPUT_REG);
	writel(priv->port_irq_pm, priv->base + PORT_IRQ_ENABLE_REG);
	return 0;
}
#endif

static const struct dev_pm_ops sdc_spi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdc_spi_suspend, sdc_spi_resume)
};

static struct platform_driver sdc_spi_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &sdc_spi_pm_ops,
	},
	.probe = sdc_spi_probe,
	.remove = sdc_spi_remove,
};

static int __init sdc_spi_init(void)
{
	int ret;

#ifdef CONFIG_PROC_FS
	driver_ent = proc_mkdir(DRV_NAME, NULL);
	if (!driver_ent)
		pr_warn("failed to create driver proc entry\n");
#endif

	ret = platform_driver_register(&sdc_spi_driver);
	if (ret < 0)
		goto out_remove_proc_entry;

	return 0;

out_remove_proc_entry:
#ifdef CONFIG_PROC_FS
	if (driver_ent)
		remove_proc_entry(DRV_NAME, NULL);
#endif
	return ret;
}
module_init(sdc_spi_init);

static void __exit sdc_spi_exit(void)
{
	platform_driver_unregister(&sdc_spi_driver);

#ifdef CONFIG_PROC_FS
	if (driver_ent)
		remove_proc_entry(DRV_NAME, NULL);
#endif
}
module_exit(sdc_spi_exit);

MODULE_AUTHOR("Jason Lee <jason_lee@sunix.com>");
MODULE_DESCRIPTION("SUNIX SDC SPI controller driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);

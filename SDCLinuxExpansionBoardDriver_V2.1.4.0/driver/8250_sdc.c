// SPDX-License-Identifier: GPL-2.0
/*
 * 8250_sdc - SUNIX SDC serial port driver
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
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/serial_8250.h>
#include "sunix-sdc.h"

#define DRV_NAME				"8250_sdc"

#define SDC_UART_UMR				0x0e

#define SDC_UART_RS232				BIT(0)
#define SDC_UART_RS422				BIT(1)
#define SDC_UART_RS485				BIT(2)
#define SDC_UART_MODE_MASK			GENMASK(2, 0)
#define SDC_UART_AHDC				BIT(3)
#define SDC_UART_CS					BIT(4)
#define SDC_UART_AUTO_RS422485		BIT(5)
#define SDC_UART_RS422_TERMINATION	BIT(6)
#define SDC_UART_RS485_TERMINATION	BIT(7)

#if KERNEL_VERSION(4, 13, 0) > LINUX_VERSION_CODE
#ifndef SER_RS485_TERMINATE_BUS
#define SER_RS485_TERMINATE_BUS	(1 << 5)
#endif
#endif

struct sdc8250_port {
	struct sdc_uart_platdata *pdata;
	struct platform_device *pdev;
	unsigned long base;
	int line;

#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *dev_ent;
#endif

	u8 umr_pm;
};

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *driver_ent;
#endif

static void sdc8250_do_pm(struct uart_port *p, unsigned int state,
								unsigned int old)
{
	if (!state)
		pm_runtime_get_sync(p->dev);

	serial8250_do_pm(p, state, old);

	if (state)
		pm_runtime_put_sync_suspend(p->dev);
}

#if KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE
static int sdc8250_rs485_config(struct uart_port *p, struct ktermios *termios,
								struct serial_rs485 *rs485)
#else
static int sdc8250_rs485_config(struct uart_port *p, struct serial_rs485 *rs485)
#endif
{
	struct sdc_uart_platdata *pdata;
	struct sdc8250_port *priv;
	u32 flags;
	int is_rs485 = !!(rs485->flags & SER_RS485_ENABLED);
	int ret = 0;
	u8 umr;

	priv = p->private_data;
	pdata = priv->pdata;
	flags = rs485->flags;

	switch (pdata->version) {
	case 0x00:
		if (is_rs485) {
			if (flags & SER_RS485_RX_DURING_TX) {
				/* Set RS422 */
				if (!(pdata->capability & SDC_UART_RS422)) {
					ret = -EINVAL;
					break;
				}
				umr = SDC_UART_RS422;

				if (flags & SER_RS485_TERMINATE_BUS) {
					if (!(pdata->capability & SDC_UART_RS422_TERMINATION)) {
						ret = -EINVAL;
						break;
					}
					umr |= SDC_UART_RS422_TERMINATION;
				}
			} else {
				/* Set RS485 */
				if (!(pdata->capability & SDC_UART_RS485)) {
					ret = -EINVAL;
					break;
				}
				umr = SDC_UART_RS485;

				if (pdata->capability & SDC_UART_AHDC)
					umr |= SDC_UART_AHDC;

				if (pdata->capability & SDC_UART_CS)
					umr |= SDC_UART_CS;

				if (flags & SER_RS485_TERMINATE_BUS) {
					if (!(pdata->capability & SDC_UART_RS485_TERMINATION)) {
						ret = -EINVAL;
						break;
					}
					umr |= SDC_UART_RS485_TERMINATION;
				}
			}
		} else {
			/* Set RS232 */
			if (!(pdata->capability & SDC_UART_RS232)) {
				ret = -EINVAL;
				break;
			}
			umr = SDC_UART_RS232;
		}

		outb(umr, p->iobase + SDC_UART_UMR);
#if KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE
		p->rs485 = *rs485;
#endif
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct serial_rs485 sdc8250_rs485_supported = {
	.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND |
			SER_RS485_RX_DURING_TX | SER_RS485_TERMINATE_BUS,
};

static int sdc8250_mode_init(struct sdc8250_port *priv,
								struct serial_rs485 *rs485)
{
	struct sdc_uart_platdata *pdata = priv->pdata;
	int is_rs485 = !!(pdata->capability & (SDC_UART_RS422 | SDC_UART_RS485));
	int ret = 0;
	u8 umr;

#if KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE
	memcpy(rs485, &sdc8250_rs485_supported, sizeof(struct serial_rs485));
#else
	memset(rs485, 0, sizeof(struct serial_rs485));
	if (pdata->capability & (SDC_UART_RS422 | SDC_UART_RS485))
		rs485->flags |= SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND;
	if (pdata->capability & (SDC_UART_RS422_TERMINATION | SDC_UART_RS485_TERMINATION))
		rs485->flags |= SER_RS485_TERMINATE_BUS;
	if (pdata->capability & SDC_UART_RS422)
		rs485->flags |= SER_RS485_RX_DURING_TX;
#endif

	switch (pdata->version) {
	case 0:
		/* set UMR value */
		if (is_rs485) {
			if (pdata->capability & SDC_UART_RS422) {
				/* Set UMR to RS422 */
				umr = SDC_UART_RS422;

				outb(umr, priv->base + SDC_UART_UMR);

			} else if (pdata->capability & SDC_UART_RS485) {
				/* Set UMR to RS485 */
				umr = SDC_UART_RS485;
				if (pdata->capability & SDC_UART_AHDC)
					umr |= SDC_UART_AHDC;
				if (pdata->capability & SDC_UART_CS)
					umr |= SDC_UART_CS;

				outb(umr, priv->base + SDC_UART_UMR);

			} else {
				ret = -EINVAL;
			}
		} else {
			/* Set UMR to RS232 */
			umr = SDC_UART_RS232;
			outb(umr, priv->base + SDC_UART_UMR);
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#ifdef CONFIG_PROC_FS
static int sdc8250_proc_show(struct seq_file *m, void *v)
{
	struct sdc8250_port *priv = m->private;

	seq_printf(m, "pci_bus=%d;irq=%d;index=%d;pid=%d;\n",
		priv->pdata->board.pci_bus, priv->pdata->board.irq, priv->pdata->index,
		priv->pdev->id);

	return 0;
}

#if KERNEL_VERSION(4, 18, 0) > LINUX_VERSION_CODE
static int sdc8250_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, sdc8250_proc_show, PDE_DATA(inode));
}

static const struct file_operations sdc8250_proc_fops = {
	.open		= sdc8250_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static int sdc8250_proc_create(struct sdc8250_port *priv)
{
	char buf[16] = {0};

	if (!driver_ent)
		return -ENOMEM;

	sprintf(buf, "line%d", priv->line);
#if KERNEL_VERSION(4, 18, 0) <= LINUX_VERSION_CODE
	priv->dev_ent = proc_create_single_data(buf, 0444, driver_ent,
											sdc8250_proc_show, priv);
#else
	priv->dev_ent = proc_create_data(buf, 0444, driver_ent,
											&sdc8250_proc_fops, priv);
#endif
	if (!priv->dev_ent)
		return -ENOMEM;

	return 0;
}

static void sdc8250_proc_remove(struct sdc8250_port *priv)
{
	char buf[16] = {0};

	if (priv->dev_ent) {
		sprintf(buf, "line%d", priv->line);
		remove_proc_entry(buf, driver_ent);
	}
}
#endif

static int sdc8250_probe(struct platform_device *pdev)
{
	struct sdc_uart_platdata *pdata;
	struct uart_8250_port uart;
	struct serial_rs485 rs485;
	struct sdc8250_port *priv;
	struct resource *res;
	int irq;
	int ret;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata)
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	priv->pdata = pdata;
	priv->pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		return -EINVAL;

	priv->base = res->start;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = sdc8250_mode_init(priv, &rs485);
	if (ret < 0)
		return ret;

	memset(&uart, 0, sizeof(uart));
	spin_lock_init(&uart.port.lock);
	uart.port.iotype = UPIO_PORT;
	uart.port.iobase = res->start;
	uart.port.regshift = 0;
	uart.port.irq = irq;
	uart.port.type = PORT_16550A;
	uart.port.flags = UPF_FIXED_PORT | UPF_FIXED_TYPE | UPF_SHARE_IRQ;
	uart.port.fifosize = pdata->fifo_size;
	uart.port.uartclk = pdata->clk_rate;
	uart.port.private_data = priv;
	uart.port.pm = sdc8250_do_pm;
	uart.port.rs485_config = sdc8250_rs485_config;
#if KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE
	memcpy(&uart.port.rs485_supported, &rs485, sizeof(rs485));
	/* Disable termination in default */
	rs485.flags &= ~SER_RS485_TERMINATE_BUS;
	memcpy(&uart.port.rs485, &rs485, sizeof(rs485));
#else
	/* Disable termination in default */
	rs485.flags &= ~SER_RS485_TERMINATE_BUS;
	memcpy(&uart.port.rs485, &rs485, sizeof(rs485));
#endif

	ret = serial8250_register_8250_port(&uart);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register serial port, err %d\n", ret);
		return ret;
	}
	priv->line = ret;

#ifdef CONFIG_PROC_FS
	ret = sdc8250_proc_create(priv);
	if (ret < 0)
		dev_warn(&pdev->dev, "failed to create device proc entry\n");
#endif
	return 0;
}

#if KERNEL_VERSION(6, 11, 0) <= LINUX_VERSION_CODE
static void sdc8250_remove(struct platform_device *pdev)
#else
static int sdc8250_remove(struct platform_device *pdev)
#endif
{
	struct sdc8250_port *priv = platform_get_drvdata(pdev);

#ifdef CONFIG_PROC_FS
	sdc8250_proc_remove(priv);
#endif

	serial8250_unregister_port(priv->line);

#if KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE
	return 0;
#endif
}

#ifdef CONFIG_PM_SLEEP
static int sdc8250_suspend(struct device *dev)
{
	struct sdc8250_port *priv = dev_get_drvdata(dev);

	priv->umr_pm = inb(priv->base + SDC_UART_UMR);
	serial8250_suspend_port(priv->line);
	return 0;
}

static int sdc8250_resume(struct device *dev)
{
	struct sdc8250_port *priv = dev_get_drvdata(dev);

	outb(priv->umr_pm, priv->base + SDC_UART_UMR);
	serial8250_resume_port(priv->line);
	return 0;
}
#endif

static const struct dev_pm_ops sdc8250_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdc8250_suspend, sdc8250_resume)
};

static struct platform_driver sdc8250_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &sdc8250_pm_ops,
	},
	.probe = sdc8250_probe,
	.remove = sdc8250_remove,
};

static int __init sdc8250_init(void)
{
	int ret;

#ifdef CONFIG_PROC_FS
	driver_ent = proc_mkdir(DRV_NAME, NULL);
	if (!driver_ent)
		pr_warn("failed to create driver proc entry\n");
#endif

	ret = platform_driver_register(&sdc8250_driver);
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
module_init(sdc8250_init);

static void __exit sdc8250_exit(void)
{
	platform_driver_unregister(&sdc8250_driver);

#ifdef CONFIG_PROC_FS
	if (driver_ent)
		remove_proc_entry(DRV_NAME, NULL);
#endif
}
module_exit(sdc8250_exit);

MODULE_AUTHOR("Jason Lee <jason_lee@sunix.com>");
MODULE_DESCRIPTION("SUNIX SDC serial port driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);

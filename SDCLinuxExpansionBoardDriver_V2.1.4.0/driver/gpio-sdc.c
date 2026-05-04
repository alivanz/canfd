// SPDX-License-Identifier: GPL-2.0
/*
 * sdc_gpio - SUNIX SDC GPIO controller driver
 *
 * Copyright (c) 2025 - 2026 SUNIX Co., Ltd. <info@sunix.com>
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
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/gpio/driver.h>
#if KERNEL_VERSION(4, 8, 0) > LINUX_VERSION_CODE
#include <linux/sched.h>
#endif
#include "sdc_config.h"

#define GPIO_DEBUG				0
#if GPIO_DEBUG
#define GPRINFO(format, ...)	pr_info(""format"", ##__VA_ARGS__)
#else
#define GPRINFO(format, ...)
#endif

#ifndef GPIO_LINE_DIRECTION_IN
#define GPIO_LINE_DIRECTION_IN	1
#endif
#ifndef GPIO_LINE_DIRECTION_OUT
#define GPIO_LINE_DIRECTION_OUT	0
#endif

#define DRV_NAME				"sdc_gpio"

enum {
	REG_INPUT,
	REG_INPUT_DELTA,
	REG_INPUT_INVERT_ENABLE,
	REG_INPUT_LATCH_POSITIVE,
	REG_INPUT_LATCH_NEGATIVE,
	REG_DI_COUNTER_RESET,
	REG_DI_COUNTER_INCREMENT_POSITIVE,
	REG_DI_COUNTER_INCREMENT_NEGATIVE,
	REG_DI_RISING_EVENT,
	REG_DI_FALLING_EVENT,
	REG_OUTPUT_WRITE_ENABLE,
	REG_OUTPUT,
	REG_DIRECTION_CTRL,
	REG_OUTPUT_INITIAL_VALUE,
	REG_MAX
};

#define REG_DI_FILTER_BASE						64

#define GPIO_INT_RISING							(1 << 1)
#define GPIO_INT_FALLING						(2 << 1)
#define GPIO_INT_BOTH							(3 << 1)

#define GPIO_IO_MAX								32

struct sdc_gpio {
	struct sdc_dio_platdata *pdata;
	struct platform_device *pdev;
	int irq;
	void __iomem *base;
	int base_size;
	void __iomem *event;

	struct gpio_chip gpio_chip;
	struct device *dev;
	struct mutex lock;

	int nr_gpio;
	int bank_idx[GPIO_IO_MAX];

#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *dev_ent;
#endif

	unsigned int irq_type[GPIO_IO_MAX];
	bool irq_enabled[GPIO_IO_MAX];
	u32 reg_pm[REG_MAX];
	u32 reg_pm_filter[GPIO_IO_MAX];
};

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *driver_ent;
#endif

static inline u32 sdc_gpio_rl(struct sdc_gpio *priv, int reg)
{
	return readl(priv->base + (reg * 4));
}

static inline void sdc_gpio_wl(struct sdc_gpio *priv, int reg, int value)
{
	writel(value, priv->base + (reg * 4));
}

static int sdc_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct sdc_gpio *priv = gpiochip_get_data(gc);
	int reg;

	reg = sdc_gpio_rl(priv, REG_DIRECTION_CTRL);
	if (reg & BIT(offset))
		reg = sdc_gpio_rl(priv, REG_OUTPUT);
	else
		reg = sdc_gpio_rl(priv, REG_INPUT);

	return !!(reg & BIT(offset));
}

static int sdc_gpio_get_multiple(struct gpio_chip *gc, unsigned long *mask,
								unsigned long *bits)
{
	struct sdc_gpio *priv = gpiochip_get_data(gc);
	int reg;

	reg = sdc_gpio_rl(priv, REG_OUTPUT);
	reg |= sdc_gpio_rl(priv, REG_INPUT);
	*bits = reg & *mask;

	return 0;
}

#if KERNEL_VERSION(6, 17, 0) <= LINUX_VERSION_CODE
static int sdc_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
#else
static void sdc_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
#endif
{
	struct sdc_gpio *priv = gpiochip_get_data(gc);
	int b_idx, reg;

	b_idx = priv->bank_idx[offset];
	if (b_idx < 0)
#if KERNEL_VERSION(6, 17, 0) <= LINUX_VERSION_CODE
		return 0;
#else
		return;
#endif

	if (!priv->pdata->banks[b_idx].cap_output)
#if KERNEL_VERSION(6, 17, 0) <= LINUX_VERSION_CODE
		return 0;
#else
		return;
#endif

	mutex_lock(&priv->lock);

	reg = sdc_gpio_rl(priv, REG_OUTPUT);
	if (value)
		reg |= BIT(offset);
	else
		reg &= ~BIT(offset);

	sdc_gpio_wl(priv, REG_OUTPUT_WRITE_ENABLE, reg | BIT(offset));
	sdc_gpio_wl(priv, REG_OUTPUT, reg);

	mutex_unlock(&priv->lock);
	
#if KERNEL_VERSION(6, 17, 0) <= LINUX_VERSION_CODE
	return 0;
#endif
}

#if KERNEL_VERSION(6, 17, 0) <= LINUX_VERSION_CODE
static int sdc_gpio_set_multiple(struct gpio_chip *gc, unsigned long *mask,
								unsigned long *bits)
#else
static void sdc_gpio_set_multiple(struct gpio_chip *gc, unsigned long *mask,
								unsigned long *bits)
#endif
{
	struct sdc_gpio *priv = gpiochip_get_data(gc);
	int reg;

	mutex_lock(&priv->lock);

	reg = *bits & *mask;

	sdc_gpio_wl(priv, REG_OUTPUT_WRITE_ENABLE, *mask);
	sdc_gpio_wl(priv, REG_OUTPUT, reg);

	mutex_unlock(&priv->lock);
	
#if KERNEL_VERSION(6, 17, 0) <= LINUX_VERSION_CODE
	return 0;
#endif
}

static int sdc_gpio_input(struct gpio_chip *gc, unsigned int offset)
{
	struct sdc_gpio *priv = gpiochip_get_data(gc);
	int b_idx, reg;

	b_idx = priv->bank_idx[offset];
	if (b_idx < 0)
		return -EINVAL;

	if (!priv->pdata->banks[b_idx].cap_input)
		return -ENOTSUPP;

	mutex_lock(&priv->lock);

	reg = sdc_gpio_rl(priv, REG_DIRECTION_CTRL);
	reg &= ~(BIT(offset));
	sdc_gpio_wl(priv, REG_DIRECTION_CTRL, reg);

	mutex_unlock(&priv->lock);
	return 0;
}

static int sdc_gpio_output(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct sdc_gpio *priv = gpiochip_get_data(gc);
	int b_idx, reg;

	b_idx = priv->bank_idx[offset];
	if (b_idx < 0)
		return -EINVAL;

	if (!priv->pdata->banks[b_idx].cap_output)
		return -ENOTSUPP;

	mutex_lock(&priv->lock);

	reg = sdc_gpio_rl(priv, REG_DIRECTION_CTRL);
	reg |= BIT(offset);
	sdc_gpio_wl(priv, REG_DIRECTION_CTRL, reg);

	reg = sdc_gpio_rl(priv, REG_OUTPUT);
	if (value)
		reg |= BIT(offset);
	else
		reg &= ~BIT(offset);

	sdc_gpio_wl(priv, REG_OUTPUT_WRITE_ENABLE, reg | BIT(offset));
	sdc_gpio_wl(priv, REG_OUTPUT, reg);

	mutex_unlock(&priv->lock);
	return 0;
}

static int sdc_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct sdc_gpio *priv = gpiochip_get_data(gc);
	int reg, ret;

	reg = sdc_gpio_rl(priv, REG_DIRECTION_CTRL);
	ret = reg & BIT(offset) ?
			GPIO_LINE_DIRECTION_OUT :
			GPIO_LINE_DIRECTION_IN;

	return ret;
}

static int sdc_gpio_set_debounce(struct gpio_chip *gc, unsigned int offset,
								u32 debounce)
{
	struct sdc_gpio *priv = gpiochip_get_data(gc);
	u32 count = 0;

	/* Version 0 doesn't support debounce */
	if (priv->pdata->version < 1)
		return -ENOTSUPP;

	if (debounce) {
		switch (priv->pdata->di_sampling_freq) {
		case 2000:
			count = debounce / 500;
			break;
		case 500000:
			count = debounce / 2;
			break;
		default:
			return -ENOTSUPP;
		}

		if (count < priv->pdata->di_filter_min ||
			count > priv->pdata->di_filter_max)
			return -EINVAL;
	}

	sdc_gpio_wl(priv, REG_DI_FILTER_BASE + offset, count);
	return 0;
}

static int sdc_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
								unsigned long config)
{
	u32 config_para = pinconf_to_config_param(config);
	u32 config_arg;

	switch (config_para) {
	case PIN_CONFIG_INPUT_DEBOUNCE:
		config_arg = pinconf_to_config_argument(config);
		return sdc_gpio_set_debounce(gc, offset, config_arg);
	}

	return -ENOTSUPP;
}

static void sdc_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sdc_gpio *priv = gpiochip_get_data(gc);

	priv->irq_enabled[d->hwirq] = false;
#if KERNEL_VERSION(4, 20, 0) <= LINUX_VERSION_CODE
	gpiochip_disable_irq(gc, d->hwirq);
#endif
}

static void sdc_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sdc_gpio *priv = gpiochip_get_data(gc);

#if KERNEL_VERSION(4, 20, 0) <= LINUX_VERSION_CODE
	gpiochip_enable_irq(gc, d->hwirq);
#endif
	priv->irq_enabled[d->hwirq] = true;
}

static int sdc_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sdc_gpio *priv = gpiochip_get_data(gc);
	unsigned int irq_type;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		irq_type = GPIO_INT_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		irq_type = GPIO_INT_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		irq_type = GPIO_INT_RISING | GPIO_INT_FALLING;
		break;
	default:
		return -EINVAL;
	}

	priv->irq_type[d->hwirq] = irq_type;
	return 0;
}

static void sdc_gpio_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sdc_gpio *priv = gpiochip_get_data(gc);

	mutex_lock(&priv->lock);
}

static void sdc_gpio_irq_bus_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sdc_gpio *priv = gpiochip_get_data(gc);
	int reg;

	if (priv->irq_type[d->hwirq] & GPIO_INT_RISING) {
		reg = sdc_gpio_rl(priv, REG_DI_RISING_EVENT);

		if (priv->irq_enabled[d->hwirq])
			reg |= BIT(d->hwirq);
		else
			reg &= ~BIT(d->hwirq);

		sdc_gpio_wl(priv, REG_DI_RISING_EVENT, reg);
	}

	if (priv->irq_type[d->hwirq] & GPIO_INT_FALLING) {
		reg = sdc_gpio_rl(priv, REG_DI_FALLING_EVENT);

		if (priv->irq_enabled[d->hwirq])
			reg |= BIT(d->hwirq);
		else
			reg &= ~BIT(d->hwirq);

		sdc_gpio_wl(priv, REG_DI_FALLING_EVENT, reg);
	}

	mutex_unlock(&priv->lock);
}

static irqreturn_t sdc_gpio_ist(int irq, void *dev_id)
{
	struct sdc_gpio *priv = dev_id;
	unsigned int delta, offset;
	unsigned long pending;

	delta = readl(priv->event);
	if (!delta)
		return IRQ_NONE;

	pending = delta;

	for_each_set_bit(offset, &pending, priv->nr_gpio) {
		unsigned int virq;

		virq = irq_find_mapping(priv->gpio_chip.irq.domain, offset);
		handle_nested_irq(virq);
	}

	return IRQ_HANDLED;
}

static irqreturn_t sdc_gpio_irq_handler(int irq, void *dev_id)
{
	return IRQ_WAKE_THREAD;
}

static struct irq_chip sdc_gpio_irq_chip = {
	.name = "sdc_gpio",
	.irq_mask = sdc_gpio_irq_mask,
	.irq_unmask = sdc_gpio_irq_unmask,
	.irq_set_type = sdc_gpio_irq_set_type,
	.irq_bus_lock = sdc_gpio_irq_bus_lock,
	.irq_bus_sync_unlock = sdc_gpio_irq_bus_sync_unlock,
#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
#endif
};

#ifdef CONFIG_PROC_FS
static int sdc_gpio_proc_show(struct seq_file *m, void *v)
{
	struct sdc_gpio *priv = m->private;

	seq_printf(m, "pci_bus=%d;irq=%d;index=%d;pid=%d;label=%s;\n",
		priv->pdata->board.pci_bus, priv->pdata->board.irq, priv->pdata->index,
		priv->pdev->id, priv->gpio_chip.label);

	return 0;
}

#if KERNEL_VERSION(4, 18, 0) > LINUX_VERSION_CODE
static int sdc_gpio_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, sdc_gpio_proc_show, PDE_DATA(inode));
}

static const struct file_operations sdc_gpio_proc_fops = {
	.open		= sdc_gpio_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static int sdc_gpio_proc_create(struct sdc_gpio *priv)
{
	char buf[16] = {0};

	if (!driver_ent)
		return -ENOMEM;

	sprintf(buf, "id%d", priv->pdev->id);
#if KERNEL_VERSION(4, 18, 0) <= LINUX_VERSION_CODE
	priv->dev_ent =	proc_create_single_data(buf, 0444, driver_ent,
											sdc_gpio_proc_show, priv);
#else
	priv->dev_ent = proc_create_data(buf, 0444, driver_ent,
											&sdc_gpio_proc_fops, priv);
#endif
	if (!priv->dev_ent)
		return -ENOMEM;

	return 0;
}

static void sdc_gpio_proc_remove(struct sdc_gpio *priv)
{
	char buf[16] = {0};

	if (priv->dev_ent) {
		sprintf(buf, "id%d", priv->pdev->id);
		remove_proc_entry(buf, driver_ent);
	}
}
#endif

static int sdc_gpio_probe(struct platform_device *pdev)
{
	struct sdc_dio_platdata *pdata;
	struct gpio_irq_chip *girq;
	struct sdc_gpio *priv;
	struct resource *res;
	int ret, i, j, total_io;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata)
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	for (i = 0; i < pdata->nr_bank; i++)
		priv->nr_gpio += pdata->banks[i].nr_io;
	if (priv->nr_gpio > GPIO_IO_MAX)
		return -EINVAL;

	platform_set_drvdata(pdev, priv);
	priv->pdata = pdata;
	priv->pdev = pdev;

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->base_size = resource_size(res);
	if (priv->base_size < 0)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	priv->event = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->event))
		return PTR_ERR(priv->event);

	priv->dev = &pdev->dev;

	mutex_init(&priv->lock);

	priv->gpio_chip.get = sdc_gpio_get;
	priv->gpio_chip.get_multiple = sdc_gpio_get_multiple;
	priv->gpio_chip.set = sdc_gpio_set;
	priv->gpio_chip.set_multiple = sdc_gpio_set_multiple;
	priv->gpio_chip.set = sdc_gpio_set;
	priv->gpio_chip.set_multiple = sdc_gpio_set_multiple;
	priv->gpio_chip.direction_input = sdc_gpio_input;
	priv->gpio_chip.direction_output = sdc_gpio_output;
	priv->gpio_chip.get_direction = sdc_gpio_get_direction;
	priv->gpio_chip.set_config = sdc_gpio_set_config;
	priv->gpio_chip.label = dev_name(&pdev->dev);
	priv->gpio_chip.owner = THIS_MODULE;
	priv->gpio_chip.parent = &pdev->dev;
	priv->gpio_chip.ngpio = priv->nr_gpio;
	priv->gpio_chip.can_sleep = false;
	priv->gpio_chip.base = -1;

	girq = &priv->gpio_chip.irq;

#if KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE
	gpio_irq_chip_set_chip(girq, &sdc_gpio_irq_chip);
#else
	girq->chip = &sdc_gpio_irq_chip;
#endif
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_simple_irq;
	girq->threaded = true;

	memset(priv->bank_idx, -1, sizeof(int) * GPIO_IO_MAX);
	for (i = 0; i < GPIO_IO_MAX; i++) {
		total_io = 0;
		for (j = 0; j < pdata->nr_bank; j++) {
			total_io += pdata->banks[j].nr_io;
			if (i < total_io) {
				priv->bank_idx[i] = j;
				break;
			}
		}
	}

	sdc_gpio_wl(priv, REG_DI_RISING_EVENT, 0);
	sdc_gpio_wl(priv, REG_DI_FALLING_EVENT, 0);

	ret = request_threaded_irq(priv->irq, sdc_gpio_irq_handler, sdc_gpio_ist,
							IRQF_SHARED, dev_name(&pdev->dev), priv);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to attach interrupt, err %d\n", ret);
		return ret;
	}

	ret = devm_gpiochip_add_data(priv->dev, &priv->gpio_chip, priv);
	if (ret < 0) {
		free_irq(priv->irq, priv);
		dev_err(&pdev->dev, "failed to add gpiochip, err %d\n", ret);
		return ret;
	}

	dev_set_drvdata(priv->dev, priv);

#ifdef CONFIG_PROC_FS
	ret = sdc_gpio_proc_create(priv);
	if (ret < 0)
		dev_warn(&pdev->dev, "failed to create device proc entry\n");
#endif

	return 0;
}

#if KERNEL_VERSION(6, 11, 0) <= LINUX_VERSION_CODE
static void sdc_gpio_remove(struct platform_device *pdev)
#else
static int sdc_gpio_remove(struct platform_device *pdev)
#endif
{
	struct sdc_gpio *priv = platform_get_drvdata(pdev);

#ifdef CONFIG_PROC_FS
	sdc_gpio_proc_remove(priv);
#endif

	sdc_gpio_wl(priv, REG_DI_RISING_EVENT, 0);
	sdc_gpio_wl(priv, REG_DI_FALLING_EVENT, 0);

	free_irq(priv->irq, priv);

#if KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE
	return 0;
#endif
}

#ifdef CONFIG_PM_SLEEP
static int sdc_gpio_suspend(struct device *dev)
{
	struct sdc_gpio *priv = dev_get_drvdata(dev);
	int i;

	priv->reg_pm[8] = sdc_gpio_rl(priv, REG_DI_RISING_EVENT);
	priv->reg_pm[9] = sdc_gpio_rl(priv, REG_DI_FALLING_EVENT);
	priv->reg_pm[11] = sdc_gpio_rl(priv, REG_OUTPUT);
	priv->reg_pm[12] = sdc_gpio_rl(priv, REG_DIRECTION_CTRL);

	if (priv->pdata->version > 1)
		for (i = 0; i < GPIO_IO_MAX; i++)
			priv->reg_pm_filter[i] = sdc_gpio_rl(priv, REG_DI_FILTER_BASE + i);

	return 0;
}

static int sdc_gpio_resume(struct device *dev)
{
	struct sdc_gpio *priv = dev_get_drvdata(dev);
	int i;

	sdc_gpio_wl(priv, REG_DIRECTION_CTRL, priv->reg_pm[12]);
	sdc_gpio_wl(priv, REG_OUTPUT_WRITE_ENABLE, priv->reg_pm[12]);
	sdc_gpio_wl(priv, REG_OUTPUT, priv->reg_pm[11]);
	sdc_gpio_wl(priv, REG_DI_FALLING_EVENT, priv->reg_pm[9]);
	sdc_gpio_wl(priv, REG_DI_RISING_EVENT, priv->reg_pm[8]);

	if (priv->pdata->version > 1)
		for (i = 0; i < GPIO_IO_MAX; i++)
			sdc_gpio_wl(priv, REG_DI_FILTER_BASE + i, priv->reg_pm_filter[i]);

	return 0;
}
#endif

static const struct dev_pm_ops sdc_gpio_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdc_gpio_suspend, sdc_gpio_resume)
};

static struct platform_driver sdc_gpio_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &sdc_gpio_pm_ops,
	},
	.probe = sdc_gpio_probe,
	.remove = sdc_gpio_remove,
};

static int __init sdc_gpio_init(void)
{
#ifdef CONFIG_PROC_FS
	driver_ent = proc_mkdir(DRV_NAME, NULL);
	if (!driver_ent)
		pr_warn("failed to create driver proc entry\n");
#endif

	return platform_driver_register(&sdc_gpio_driver);
}
module_init(sdc_gpio_init);

static void __exit sdc_gpio_exit(void)
{
	platform_driver_unregister(&sdc_gpio_driver);

#ifdef CONFIG_PROC_FS
	if (driver_ent)
		remove_proc_entry(DRV_NAME, NULL);
#endif
}
module_exit(sdc_gpio_exit);

MODULE_AUTHOR("Jason Lee <jason_lee@sunix.com>");
MODULE_DESCRIPTION("SUNIX SDC GPIO controller driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);

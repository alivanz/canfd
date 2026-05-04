// SPDX-License-Identifier: GPL-2.0
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
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#if KERNEL_VERSION(4, 8, 0) > LINUX_VERSION_CODE
#include <linux/sched.h>
#endif
#include "sdc_config.h"

#define DRV_NAME				"sdc_config"
#define MAX_DEVS				256

#define CTRL_REG				0x00
#define STATUS_REG				0x04
#define NVM_CTRL_REG			0x08
#define NVM_ADDR_REG			0x0C
#define NVM_RAM_REG				0x100
/* NVM RAM 256 byte */

/* Control register bit fields */
#define CTRL_START				BIT(0)
#define CTRL_INTERRUPT_EN		BIT(1)

/* Status register bit fields */
#define STATUS_BUSY				BIT(0)
#define STATUS_PROCESS_FINISH	BIT(1)

/* NVM control register byte 0 */
#define NVM_CMD_READ			0x01
#define NVM_CMD_WRITE			0x02
#define NVM_CMD_ERASE			0x03
/* NVM control register byte 1 */
#define NVM_PA_CTRL				0x01
/* NVM control register byte 2 ~ 3, read/write length */

struct sdc_config {
	struct sdc_config_platdata *pdata;
	struct platform_device *pdev;
	struct list_head list;
	int irq;
	void __iomem *info;
	int info_size;
	void __iomem *base;
	int base_size;

	struct device *dev;
	struct cdev cdev;
	int minor;
	struct mutex i_mutex;

#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *dev_ent;
#endif
};

static DEFINE_IDA(dev_nrs);
static dev_t first_devt;
static struct class *dev_class;
#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *driver_ent;
#endif
static LIST_HEAD(config_list);
static DEFINE_MUTEX(config_lock);

static int sdc_config_open(struct inode *inode, struct file *filp)
{
	struct sdc_config *priv = container_of(inode->i_cdev,
										struct sdc_config, cdev);

	filp->private_data = priv;
	return 0;
}

static int sdc_config_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int sdc_config_aviable(struct sdc_config *priv)
{
	unsigned long timeout;

	/* Wait for the controller not busy */
	timeout = jiffies + HZ;
	while (readl(priv->base + STATUS_REG) & STATUS_BUSY) {
		schedule();
		if (time_after(jiffies, timeout)) {
			dev_err(priv->dev, "controller busy\n");
			return -EBUSY;
		}
	}
	return 0;
}

static int sdc_config_finish(struct sdc_config *priv)
{
	unsigned long timeout;

	/* Wait for the controller finish */
	timeout = jiffies + HZ;
	while (!(readl(priv->base + STATUS_REG) & STATUS_PROCESS_FINISH)) {
		schedule();
		if (time_after(jiffies, timeout)) {
			dev_err(priv->dev, "controller process timeout\n");
			return -EBUSY;
		}
	}
	return 0;
}

static int sdc_config_erase_dio(struct sdc_config *priv)
{
	__u32 reg;
	int ret;

	if (mutex_lock_interruptible(&priv->i_mutex))
		return -EINTR;

	ret = sdc_config_aviable(priv);
	if (ret < 0)
		goto out;

	/* set NVM ctrl, erase 1 unit (4k byte) */
	reg = NVM_CMD_ERASE | (1 << 16);
	writel(reg, priv->base + NVM_CTRL_REG);
	/* set NVM addr, dio settings fixed in 0x1000 */
	writel(0x1000, priv->base + NVM_ADDR_REG);
	/* set ctrl reg, start process */
	writel(CTRL_START, priv->base + CTRL_REG);

	ret = sdc_config_finish(priv);

out:
	mutex_unlock(&priv->i_mutex);
	return ret;
}

static int sdc_config_set_dio(struct sdc_config *priv, struct config_dio *dio)
{
	__u32 reg;
	int ret, i;

	if (mutex_lock_interruptible(&priv->i_mutex))
		return -EINTR;

	ret = sdc_config_aviable(priv);
	if (ret < 0)
		goto out;

	/* set NVM ctrl, write length in byte */
	reg = NVM_CMD_WRITE | (((dio->nr_bank + 2) * 4) << 16);
	writel(reg, priv->base + NVM_CTRL_REG);
	/* set NVM addr, dio settings fixed in 0x1000 */
	writel(0x1000, priv->base + NVM_ADDR_REG);
	/* set NVM data */
	writel(dio->direction, priv->base + NVM_RAM_REG);
	writel(dio->output_initial, priv->base + NVM_RAM_REG + 4);
	for (i = 0; i < dio->nr_bank; i++)
		writel(dio->bank[i], priv->base + NVM_RAM_REG + 8 + (i * 4));
	/* set ctrl reg, start process */
	writel(CTRL_START, priv->base + CTRL_REG);

	ret = sdc_config_finish(priv);

out:
	mutex_unlock(&priv->i_mutex);
	return ret;
}

static int sdc_config_get_dio(struct sdc_config *priv, struct config_dio *dio)
{
	__u32 reg;
	int ret, i;

	if (mutex_lock_interruptible(&priv->i_mutex))
		return -EINTR;

	ret = sdc_config_aviable(priv);
	if (ret < 0)
		goto out;

	/* set NVM ctrl, read length in byte*/
	reg = NVM_CMD_READ | (((dio->nr_bank + 2) * 4) << 16);
	writel(reg, priv->base + NVM_CTRL_REG);
	/* set NVM addr, dio settings fixed in 0x1000 */
	writel(0x1000, priv->base + NVM_ADDR_REG);
	/* set ctrl reg, start process */
	writel(CTRL_START, priv->base + CTRL_REG);

	ret = sdc_config_finish(priv);
	if (ret < 0)
		goto out;

	/* get NVM data */
	dio->direction = readl(priv->base + NVM_RAM_REG);
	dio->output_initial = readl(priv->base + NVM_RAM_REG + 4);
	for (i = 0; i < dio->nr_bank; i++)
		dio->bank[i] = readl(priv->base + NVM_RAM_REG + 8 + (i * 4));

out:
	mutex_unlock(&priv->i_mutex);
	return ret;
}

static int sdc_config_erase_bitstream(struct sdc_config *priv,
								struct config_bitstream *b)
{
	__u32 reg;
	int ret;

	if (mutex_lock_interruptible(&priv->i_mutex))
		return -EINTR;

	ret = sdc_config_aviable(priv);
	if (ret < 0)
		goto out;

	/* set NVM ctrl, erase 1 unit (4k byte) */
	reg = NVM_CMD_ERASE | (NVM_PA_CTRL << 8) | (1 << 16);
	writel(reg, priv->base + NVM_CTRL_REG);
	/* set NVM addr */
	writel(b->addr, priv->base + NVM_ADDR_REG);
	/* set ctrl reg, start process */
	writel(CTRL_START, priv->base + CTRL_REG);

	ret = sdc_config_finish(priv);
	if (ret < 0)
		goto out;

out:
	mutex_unlock(&priv->i_mutex);
	return ret;
}

static int sdc_config_set_bitstream(struct sdc_config *priv,
								struct config_bitstream *b)
{
	__u32 reg;
	int ret, i;

	if (mutex_lock_interruptible(&priv->i_mutex))
		return -EINTR;

	ret = sdc_config_aviable(priv);
	if (ret < 0)
		goto out;

	/* set NVM ctrl, write length in byte*/
	reg = NVM_CMD_WRITE | (NVM_PA_CTRL << 8) | ((b->len * 4) << 16);
	writel(reg, priv->base + NVM_CTRL_REG);
	/* set NVM addr */
	writel(b->addr, priv->base + NVM_ADDR_REG);
	/* set NVM data */
	for (i = 0; i < b->len; i++)
		writel(b->data[i], priv->base + NVM_RAM_REG + (i * 4));
	/* set ctrl reg, start process */
	writel(CTRL_START, priv->base + CTRL_REG);

	ret = sdc_config_finish(priv);
	if (ret < 0)
		goto out;

out:
	mutex_unlock(&priv->i_mutex);
	return ret;
}

static int sdc_config_get_bitstream(struct sdc_config *priv,
								struct config_bitstream *b)
{
	__u32 reg;
	int ret, i;

	if (mutex_lock_interruptible(&priv->i_mutex))
		return -EINTR;

	ret = sdc_config_aviable(priv);
	if (ret < 0)
		goto out;

	/* set NVM ctrl, read length in byte*/
	reg = NVM_CMD_READ | (NVM_PA_CTRL << 8) | ((b->len * 4) << 16);
	writel(reg, priv->base + NVM_CTRL_REG);
	/* set NVM addr */
	writel(b->addr, priv->base + NVM_ADDR_REG);
	/* set ctrl reg, start process */
	writel(CTRL_START, priv->base + CTRL_REG);

	ret = sdc_config_finish(priv);
	if (ret < 0)
		goto out;

	/* get NVM data */
	for (i = 0; i < b->len; i++)
		b->data[i] = readl(priv->base + NVM_RAM_REG + (i * 4));

out:
	mutex_unlock(&priv->i_mutex);
	return ret;
}

static long sdc_config_ioctl(struct file *filp, unsigned int cmd,
								unsigned long arg)
{
	struct sdc_config *priv = filp->private_data;
	struct config_bitstream b;
	struct config_regs rs;
	struct config_reg r;
	struct config_dio dio;
	void __user *argp = (void __user *)arg;
	int i, ret;

	switch (cmd) {
	case CONFIG_GET_INFO_REG:
		if (copy_from_user(&r, argp, sizeof(r)))
			return -EFAULT;

		if (((r.offset + 1) * 4) > priv->info_size)
			return -EINVAL;

		r.value = readl(priv->info + (r.offset * 4));

		if (copy_to_user(argp, &r, sizeof(r)))
			return -EFAULT;
		break;

	case CONFIG_GET_INFO_REGS:
		if (copy_from_user(&rs, argp, sizeof(rs)))
			return -EFAULT;

		if (rs.count > CONFIG_REG_MAX)
			return -EFAULT;

		if (((rs.offset + 1) * 4) > priv->info_size)
			return -EINVAL;

		if ((rs.offset * 4 + rs.count * 4) > priv->info_size)
			return -EINVAL;

		for (i = 0; i < rs.count; i++)
			rs.value[i] = readl(priv->info + ((rs.offset + i) * 4));

		if (copy_to_user(argp, &rs, sizeof(rs)))
			return -EFAULT;
		break;

	case CONFIG_DIO_ERASE:
		ret = sdc_config_erase_dio(priv);
		if (ret < 0)
			return ret;
		break;

	case CONFIG_DIO_SET:
		if (copy_from_user(&dio, argp, sizeof(dio)))
			return -EFAULT;

		ret = sdc_config_set_dio(priv, &dio);
		if (ret < 0)
			return ret;
		break;

	case CONFIG_DIO_GET:
		if (copy_from_user(&dio, argp, sizeof(dio)))
			return -EFAULT;

		ret = sdc_config_get_dio(priv, &dio);
		if (ret < 0)
			return ret;

		if (copy_to_user(argp, &dio, sizeof(dio)))
			return -EFAULT;
		break;

	case CONFIG_BITSTREAM_ERASE:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		ret = sdc_config_erase_bitstream(priv, &b);
		if (ret < 0)
			return ret;
		break;

	case CONFIG_BITSTREAM_SET:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		ret = sdc_config_set_bitstream(priv, &b);
		if (ret < 0)
			return ret;
		break;

	case CONFIG_BITSTREAM_GET:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		ret = sdc_config_get_bitstream(priv, &b);
		if (ret < 0)
			return ret;

		if (copy_to_user(argp, &b, sizeof(b)))
			return -EFAULT;
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static const struct file_operations sdc_config_fops = {
	.owner          = THIS_MODULE,
	.open           = sdc_config_open,
	.release        = sdc_config_release,
	.unlocked_ioctl = sdc_config_ioctl,
#if KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE
	.llseek         = noop_llseek,
#else
	.llseek         = no_llseek,
#endif
};

int sdc_config_process_dio_settings(struct config_dio_settings *s)
{
	struct sdc_config *priv;
	int priv_valid = 0;
	int ret;

	mutex_lock(&config_lock);
	list_for_each_entry(priv, &config_list, list) {
		if (priv->pdata->board.pci_bus == s->pci_bus) {
			priv_valid = 1;
			break;
		}
	}
	mutex_unlock(&config_lock);

	if (!priv_valid)
		return -ENODEV;

	ret = sdc_config_erase_dio(priv);
	if (ret < 0)
		return ret;

	ret = sdc_config_set_dio(priv, &s->settings);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(sdc_config_process_dio_settings);

#ifdef CONFIG_PROC_FS
static int sdc_config_proc_show(struct seq_file *m, void *v)
{
	struct sdc_config *priv = m->private;

	seq_printf(m, "pci_bus=%d;irq=%d;index=%d;pid=%d;\n",
		priv->pdata->board.pci_bus, priv->pdata->board.irq, priv->pdata->index,
		priv->pdev->id);

	return 0;
}

#if KERNEL_VERSION(4, 18, 0) > LINUX_VERSION_CODE
static int sdc_config_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, sdc_config_proc_show, PDE_DATA(inode));
}

static const struct file_operations sdc_config_proc_fops = {
	.open		= sdc_config_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static int sdc_config_proc_create(struct sdc_config *priv)
{
	char buf[16] = {0};

	if (!driver_ent)
		return -ENOMEM;

	sprintf(buf, "line%d", priv->minor);
#if KERNEL_VERSION(4, 18, 0) <= LINUX_VERSION_CODE
	priv->dev_ent =	proc_create_single_data(buf, 0444, driver_ent,
											sdc_config_proc_show, priv);
#else
	priv->dev_ent = proc_create_data(buf, 0444, driver_ent,
											&sdc_config_proc_fops, priv);
#endif
	if (!priv->dev_ent)
		return -ENOMEM;

	return 0;
}

static void sdc_config_proc_remove(struct sdc_config *priv)
{
	char buf[16] = {0};

	if (priv->dev_ent) {
		sprintf(buf, "line%d", priv->minor);
		remove_proc_entry(buf, driver_ent);
	}
}
#endif

#if KERNEL_VERSION(6, 2, 0) <= LINUX_VERSION_CODE
static int sdc_config_uevent(const struct device *dev, struct kobj_uevent_env *env)
#else
static int sdc_config_uevent(struct device *dev, struct kobj_uevent_env *env)
#endif
{
	add_uevent_var(env, "DEVMODE=%#o", 0666);
	return 0;
}

static int sdc_config_probe(struct platform_device *pdev)
{
	struct sdc_config_platdata *pdata;
	struct sdc_config *priv;
	struct resource *res;
	struct device *dev;
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

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->info = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->info))
		return PTR_ERR(priv->info);

	priv->info_size = resource_size(res);
	if (priv->info_size < 0)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->base_size = resource_size(res);
	if (priv->base_size < 0)
		return -ENOMEM;

	mutex_init(&priv->i_mutex);

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	ret = ida_alloc(&dev_nrs, GFP_KERNEL);
	if (ret < 0)
		return ret;
	priv->minor = ret;
#else
	ret = ida_simple_get(&dev_nrs, 0, MAX_DEVS, GFP_KERNEL);
	if (ret < 0)
		return ret;
	priv->minor = ret;
#endif

	cdev_init(&priv->cdev, &sdc_config_fops);
	priv->cdev.owner = THIS_MODULE;
	ret = cdev_add(&priv->cdev, MKDEV(MAJOR(first_devt), priv->minor), 1);
	if (ret < 0)
		goto out_free_ida;

	dev = device_create(dev_class, NULL, MKDEV(MAJOR(first_devt), priv->minor),
				priv, "%s%d", DRV_NAME, priv->minor);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		dev_err(&pdev->dev, "failed to create class device, err %d\n", ret);
		goto out_cdev_del;
	}
	priv->dev = dev;

	dev_set_drvdata(priv->dev, priv);

#ifdef CONFIG_PROC_FS
	ret = sdc_config_proc_create(priv);
	if (ret < 0)
		dev_warn(&pdev->dev, "failed to create device proc entry\n");
#endif

	mutex_lock(&config_lock);
	list_add_tail(&priv->list, &config_list);
	mutex_unlock(&config_lock);

	return 0;

out_cdev_del:
	cdev_del(&priv->cdev);
out_free_ida:
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	ida_free(&dev_nrs, priv->minor);
#else
	ida_simple_remove(&dev_nrs, priv->minor);
#endif
	return ret;
}

#if KERNEL_VERSION(6, 11, 0) <= LINUX_VERSION_CODE
static void sdc_config_remove(struct platform_device *pdev)
#else
static int sdc_config_remove(struct platform_device *pdev)
#endif
{
	struct sdc_config *priv = platform_get_drvdata(pdev);

#ifdef CONFIG_PROC_FS
	sdc_config_proc_remove(priv);
#endif

	device_destroy(dev_class, MKDEV(MAJOR(first_devt), priv->minor));

	cdev_del(&priv->cdev);
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	ida_free(&dev_nrs, priv->minor);
#else
	ida_simple_remove(&dev_nrs, priv->minor);
#endif

	mutex_lock(&config_lock);
	list_del(&priv->list);
	mutex_unlock(&config_lock);

#if KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE
	return 0;
#endif
}

#ifdef CONFIG_PM_SLEEP
static int sdc_config_suspend(struct device *dev)
{
	return 0;
}

static int sdc_config_resume(struct device *dev)
{
	return 0;
}
#endif

static const struct dev_pm_ops sdc_config_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdc_config_suspend, sdc_config_resume)
};

static struct platform_driver sdc_config_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &sdc_config_pm_ops,
	},
	.probe = sdc_config_probe,
	.remove = sdc_config_remove,
};

static int __init sdc_config_init(void)
{
	int ret;

#ifdef CONFIG_PROC_FS
	driver_ent = proc_mkdir(DRV_NAME, NULL);
	if (!driver_ent)
		pr_warn("failed to create driver proc entry\n");
#endif

#if KERNEL_VERSION(6, 4, 0) <= LINUX_VERSION_CODE
	dev_class = class_create(DRV_NAME);
#else
	dev_class = class_create(THIS_MODULE, DRV_NAME);
#endif
	if (IS_ERR(dev_class)) {
		ret = PTR_ERR(dev_class);
		goto out_remove_proc_entry;
	}

	dev_class->dev_uevent = sdc_config_uevent;

	ret = alloc_chrdev_region(&first_devt, 0, MAX_DEVS, DRV_NAME);
	if (ret < 0)
		goto out_destroy_class;

	ret = platform_driver_register(&sdc_config_driver);
	if (ret < 0)
		goto out_unregister_chrdev;

	return 0;

out_unregister_chrdev:
	unregister_chrdev_region(first_devt, MAX_DEVS);
out_destroy_class:
	class_destroy(dev_class);
out_remove_proc_entry:
#ifdef CONFIG_PROC_FS
	if (driver_ent)
		remove_proc_entry(DRV_NAME, NULL);
#endif
	return ret;
}
module_init(sdc_config_init);

static void __exit sdc_config_exit(void)
{
	platform_driver_unregister(&sdc_config_driver);
	unregister_chrdev_region(first_devt, MAX_DEVS);
	class_destroy(dev_class);

#ifdef CONFIG_PROC_FS
	if (driver_ent)
		remove_proc_entry(DRV_NAME, NULL);
#endif
}
module_exit(sdc_config_exit);

MODULE_AUTHOR("Jason Lee <jason_lee@sunix.com>");
MODULE_DESCRIPTION("SUNIX SDC configuration controller driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);

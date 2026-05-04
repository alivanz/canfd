// SPDX-License-Identifier: GPL-2.0
/*
 * sdc_dio - SUNIX SDC DIO controller driver
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
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/kfifo.h>
#if KERNEL_VERSION(4, 8, 0) > LINUX_VERSION_CODE
#include <linux/sched.h>
#endif
#include "sdc_config.h"
#include "sdc_dio.h"

#define DRV_NAME				"sdc_dio"
#define MAX_DEVS				256
#define MAX_FIFOS				1024

#define DIO_REG_INPUT							0
#define DIO_REG_INPUT_DELTA						4
#define DIO_REG_INPUT_INVERT_ENABLE				8
#define DIO_REG_INPUT_LATCH_POSITIVE			12
#define DIO_REG_INPUT_LATCH_NEGATIVE			16
#define DIO_REG_DI_COUNTER_RESET				20
#define DIO_REG_DI_COUNTER_INCREMENT_POSITIVE	24
#define DIO_REG_DI_COUNTER_INCREMENT_NEGATIVE	28
#define DIO_REG_DI_RISING_EVENT					32
#define DIO_REG_DI_FALLING_EVENT				36
#define DIO_REG_OUTPUT_WRITE_ENABLE				40
#define DIO_REG_OUTPUT							44
#define DIO_REG_DIRECTION_CTRL					48
#define DIO_REG_OUTPUT_INITIAL_VALUE			52

#define DIO_REG_BANK_CTRL_BASE					64
#define DIO_REG_BANK_IO_COUNTER_BASE			128
#define DIO_REG_BANK_IO_FILTER_BASE				256

struct sdc_dio {
	struct sdc_dio_platdata *pdata;
	struct platform_device *pdev;
	int irq;
	void __iomem *base;
	int base_size;
	void __iomem *event;

	struct device *dev;
	struct cdev cdev;
	int minor;

	struct mutex open_mutex;
	wait_queue_head_t read_wait;
	DECLARE_KFIFO_PTR(fifo, struct dio_read);
	spinlock_t fifo_lock;

#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *dev_ent;
#endif

	u32 reg_pm[14];
	u32 bank_pm[SDC_DIO_BANK_MAX];
};

static DEFINE_IDA(dev_nrs);
static dev_t first_devt;
static struct class *dev_class;
#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *driver_ent;
#endif

static int sdc_dio_open(struct inode *inode, struct file *filp)
{
	struct sdc_dio *priv = container_of(inode->i_cdev, struct sdc_dio, cdev);

	/* Allows one process to open the device */
	if (!mutex_trylock(&priv->open_mutex))
		return -EBUSY;

	get_device(priv->dev);
	kfifo_reset(&priv->fifo);

	filp->private_data = priv;
	return 0;
}

static int sdc_dio_release(struct inode *inode, struct file *filp)
{
	struct sdc_dio *priv = filp->private_data;

	writel(0, priv->base + DIO_REG_DI_RISING_EVENT);
	writel(0, priv->base + DIO_REG_DI_FALLING_EVENT);

	mutex_unlock(&priv->open_mutex);
	put_device(priv->dev);
	return 0;
}

static ssize_t sdc_dio_read(struct file *filp, char __user *buf,
								size_t count, loff_t *offset)
{
	struct sdc_dio *priv = filp->private_data;
	struct dio_read r;
	int ret;

	if (count < sizeof(r))
		return -EINVAL;

	if (kfifo_is_empty(&priv->fifo) && filp->f_flags & O_NONBLOCK)
		return -EAGAIN;

	ret = wait_event_interruptible(priv->read_wait,
										!kfifo_is_empty(&priv->fifo));
	if (ret == -ERESTARTSYS)
		return -EINTR;

	memset(&r, 0, sizeof(r));
	ret = kfifo_out_spinlocked(&priv->fifo, &r, 1, &priv->fifo_lock);
	if (ret != 1) {
		ret = -EFAULT;
		goto out;
	}

	ret = copy_to_user(buf, &r, sizeof(r));
	if (ret) {
		ret = -EFAULT;
		goto out;
	}

	ret = sizeof(r);

out:
	return ret;
}

static long sdc_dio_ioctl(struct file *filp, unsigned int cmd,
								unsigned long arg)
{
	struct dio_bank_input_counter bic;
	struct config_dio_settings s;
	struct dio_bank_io bio;
	struct sdc_dio *priv = filp->private_data;
	struct sdc_dio_platdata *pdata = priv->pdata;
	struct dio_info info;
	struct dio_bank b;
	void __user *argp = (void __user *)arg;
	u32 ctrl, temp, value, reg;
	int i, ret;

	switch (cmd) {
	case DIO_GET_INFO:
		memset(&info, 0, sizeof(info));
		info.pci_bus = pdata->board.pci_bus;
		info.irq = pdata->board.irq;
		info.line = priv->minor;
		info.index = pdata->index;
		info.version = pdata->version;
		info.capability = pdata->capability;
		info.cap_samedirection = pdata->cap_samedirection;
		info.cap_storeflash = pdata->cap_storeflash;
		info.nr_bank = pdata->nr_bank;
		info.di_sampling_freq = pdata->di_sampling_freq;
		info.di_filter_lower_bound = pdata->di_filter_lower_bound;
		info.di_filter_min = pdata->di_filter_min;
		info.di_filter_max = pdata->di_filter_max;
		for (i = 0; i < info.nr_bank; i++) {
			info.banks[i].nr_io = pdata->banks[i].nr_io;
			info.banks[i].capability = pdata->banks[i].capability;
			info.banks[i].cap_input = pdata->banks[i].cap_input;
			info.banks[i].cap_output = pdata->banks[i].cap_output;
			info.banks[i].cap_rising_trigger =
				pdata->banks[i].cap_rising_trigger;
			info.banks[i].cap_falling_trigger =
				pdata->banks[i].cap_falling_trigger;
			info.banks[i].io_mask = pdata->banks[i].io_mask;
			info.banks[i].io_shift_bits = pdata->banks[i].io_shift_bits;
		}

		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;
		break;

	case DIO_GET_BANK_DIRECTION:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1) {
			value = pdata->banks[b.bank_idx].io_mask;
			value = value >> pdata->banks[b.bank_idx].io_shift_bits;
			b.value = value;

		} else {
			b.value = 0;
		}

		if (copy_to_user(argp, &b, sizeof(b)))
			return -EFAULT;
		break;

	case DIO_SET_BANK_DIRECTION:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		temp = pdata->banks[b.bank_idx].io_mask;
		temp = temp >> pdata->banks[b.bank_idx].io_shift_bits;

		if (pdata->banks[b.bank_idx].cap_input &&
			!pdata->banks[b.bank_idx].cap_output) {
			/* Bank capability is input */
			if ((b.value & temp) != 0)
				return -EINVAL;
		}

		if (!pdata->banks[b.bank_idx].cap_input &&
			pdata->banks[b.bank_idx].cap_output) {
			/* Bank capability is output */
			if ((b.value & temp) != temp)
				return -EINVAL;
		}

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (b.value)
			ctrl = (ctrl & 0xfffffffe) | 1;
		else
			ctrl = ctrl & 0xfffffffe;

		writel(ctrl, priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		break;

	case DIO_GET_BANK_STATE:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1) {
			reg = readl(priv->base + DIO_REG_OUTPUT);
			value = reg & pdata->banks[b.bank_idx].io_mask;
			value = value >> pdata->banks[b.bank_idx].io_shift_bits;
			b.value = value;

		} else {
			reg = readl(priv->base + DIO_REG_INPUT);
			value = reg & pdata->banks[b.bank_idx].io_mask;
			value = value >> pdata->banks[b.bank_idx].io_shift_bits;
			b.value = value;
		}

		if (copy_to_user(argp, &b, sizeof(b)))
			return -EFAULT;
		break;

	case DIO_SET_BANK_STATE:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_output)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (!(ctrl & 1))
			return -EINVAL;

		reg = b.value << pdata->banks[b.bank_idx].io_shift_bits;
		reg &= pdata->banks[b.bank_idx].io_mask;

		writel(pdata->banks[b.bank_idx].io_mask,
			priv->base + DIO_REG_OUTPUT_WRITE_ENABLE);
		writel(reg, priv->base + DIO_REG_OUTPUT);
		break;

	case DIO_GET_BANK_INPUT_INVERT:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = readl(priv->base + DIO_REG_INPUT_INVERT_ENABLE);
		value = reg & pdata->banks[b.bank_idx].io_mask;
		value = value >> pdata->banks[b.bank_idx].io_shift_bits;
		b.value = value;

		if (copy_to_user(argp, &b, sizeof(b)))
			return -EFAULT;
		break;

	case DIO_SET_BANK_INPUT_INVERT:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = b.value << pdata->banks[b.bank_idx].io_shift_bits;
		reg &= pdata->banks[b.bank_idx].io_mask;

		writel(reg, priv->base + DIO_REG_INPUT_INVERT_ENABLE);
		break;

	case DIO_GET_BANK_INPUT_LATCH_POSITIVE:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = readl(priv->base + DIO_REG_INPUT_LATCH_POSITIVE);
		value = reg & pdata->banks[b.bank_idx].io_mask;
		value = value >> pdata->banks[b.bank_idx].io_shift_bits;
		b.value = value;

		if (copy_to_user(argp, &b, sizeof(b)))
			return -EFAULT;
		break;

	case DIO_SET_BANK_INPUT_LATCH_POSITIVE:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = b.value << pdata->banks[b.bank_idx].io_shift_bits;
		reg &= pdata->banks[b.bank_idx].io_mask;

		writel(reg, priv->base + DIO_REG_INPUT_LATCH_POSITIVE);
		break;

	case DIO_GET_BANK_INPUT_LATCH_NEGATIVE:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = readl(priv->base + DIO_REG_INPUT_LATCH_NEGATIVE);
		value = reg & pdata->banks[b.bank_idx].io_mask;
		value = value >> pdata->banks[b.bank_idx].io_shift_bits;
		b.value = value;

		if (copy_to_user(argp, &b, sizeof(b)))
			return -EFAULT;
		break;

	case DIO_SET_BANK_INPUT_LATCH_NEGATIVE:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = b.value << pdata->banks[b.bank_idx].io_shift_bits;
		reg &= pdata->banks[b.bank_idx].io_mask;

		writel(reg, priv->base + DIO_REG_INPUT_LATCH_NEGATIVE);
		break;

	case DIO_GET_BANK_INPUT_COUNTER_INCREMENT_POSITIVE:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = readl(priv->base + DIO_REG_DI_COUNTER_INCREMENT_POSITIVE);
		value = reg & pdata->banks[b.bank_idx].io_mask;
		value = value >> pdata->banks[b.bank_idx].io_shift_bits;
		b.value = value;

		if (copy_to_user(argp, &b, sizeof(b)))
			return -EFAULT;
		break;

	case DIO_SET_BANK_INPUT_COUNTER_INCREMENT_POSITIVE:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = b.value << pdata->banks[b.bank_idx].io_shift_bits;
		reg &= pdata->banks[b.bank_idx].io_mask;

		writel(reg, priv->base + DIO_REG_DI_COUNTER_INCREMENT_POSITIVE);
		break;

	case DIO_GET_BANK_INPUT_COUNTER_INCREMENT_NEGATIVE:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = readl(priv->base + DIO_REG_DI_COUNTER_INCREMENT_NEGATIVE);
		value = reg & pdata->banks[b.bank_idx].io_mask;
		value = value >> pdata->banks[b.bank_idx].io_shift_bits;
		b.value = value;

		if (copy_to_user(argp, &b, sizeof(b)))
			return -EFAULT;
		break;

	case DIO_SET_BANK_INPUT_COUNTER_INCREMENT_NEGATIVE:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = b.value << pdata->banks[b.bank_idx].io_shift_bits;
		reg &= pdata->banks[b.bank_idx].io_mask;

		writel(reg, priv->base + DIO_REG_DI_COUNTER_INCREMENT_NEGATIVE);
		break;

	case DIO_GET_BANK_INPUT_EVENT_CTRL_RISING:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_rising_trigger)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = readl(priv->base + DIO_REG_DI_RISING_EVENT);
		value = reg & pdata->banks[b.bank_idx].io_mask;
		value = value >> pdata->banks[b.bank_idx].io_shift_bits;
		b.value = value;

		if (copy_to_user(argp, &b, sizeof(b)))
			return -EFAULT;
		break;

	case DIO_SET_BANK_INPUT_EVENT_CTRL_RISING:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_rising_trigger)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = b.value << pdata->banks[b.bank_idx].io_shift_bits;
		reg &= pdata->banks[b.bank_idx].io_mask;

		writel(reg, priv->base + DIO_REG_DI_RISING_EVENT);
		break;

	case DIO_GET_BANK_INPUT_EVENT_CTRL_FALLING:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_falling_trigger)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = readl(priv->base + DIO_REG_DI_FALLING_EVENT);
		value = reg & pdata->banks[b.bank_idx].io_mask;
		value = value >> pdata->banks[b.bank_idx].io_shift_bits;
		b.value = value;

		if (copy_to_user(argp, &b, sizeof(b)))
			return -EFAULT;
		break;

	case DIO_SET_BANK_INPUT_EVENT_CTRL_FALLING:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_falling_trigger)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = b.value << pdata->banks[b.bank_idx].io_shift_bits;
		reg &= pdata->banks[b.bank_idx].io_mask;

		writel(reg, priv->base + DIO_REG_DI_FALLING_EVENT);
		break;

	case DIO_GET_BANK_OUTPUT_INITIAL_VALUE:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_output)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (!(ctrl & 1))
			return -EINVAL;

		reg = readl(priv->base + DIO_REG_OUTPUT_INITIAL_VALUE);
		value = reg & pdata->banks[b.bank_idx].io_mask;
		value = value >> pdata->banks[b.bank_idx].io_shift_bits;
		b.value = value;

		if (copy_to_user(argp, &b, sizeof(b)))
			return -EFAULT;
		break;

	case DIO_SET_BANK_OUTPUT_INITIAL_VALUE:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_output)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (!(ctrl & 1))
			return -EINVAL;

		reg = b.value << pdata->banks[b.bank_idx].io_shift_bits;
		reg &= pdata->banks[b.bank_idx].io_mask;

		writel(reg, priv->base + DIO_REG_OUTPUT_INITIAL_VALUE);

		if (priv->pdata->cap_storeflash) {
			/* Call config function */
			memset(&s, 0, sizeof(s));
			s.pci_bus = pdata->board.pci_bus;
			s.settings.direction = readl(priv->base + DIO_REG_DIRECTION_CTRL);
			s.settings.output_initial = reg;
			s.settings.nr_bank = pdata->nr_bank;
			for (i = 0; i < s.settings.nr_bank; i++)
				s.settings.bank[i] = readl(priv->base +
										DIO_REG_BANK_CTRL_BASE + (i * 4));

			ret = sdc_config_process_dio_settings(&s);
			if (ret)
				return ret;
		}
		break;

	case DIO_SET_BANK_INPUT_COUNTER_RESET:
		if (copy_from_user(&b, argp, sizeof(b)))
			return -EFAULT;

		if (!(b.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[b.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (b.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = b.value << pdata->banks[b.bank_idx].io_shift_bits;
		reg &= pdata->banks[b.bank_idx].io_mask;

		writel(reg, priv->base + DIO_REG_DI_COUNTER_RESET);
		break;

	case DIO_GET_BANK_INPUT_COUNTER_VALUE:
		if (copy_from_user(&bic, argp, sizeof(bic)))
			return -EFAULT;

		if (!(bic.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!pdata->banks[bic.bank_idx].cap_input)
			return -EINVAL;

		if (pdata->banks[bic.bank_idx].nr_io > DIO_BANK_IO_MAX)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (bic.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		bic.nr_io = pdata->banks[bic.bank_idx].nr_io;

		for (i = 0; i < bic.nr_io; i++)
			bic.value[i] =
				readl(priv->base + DIO_REG_BANK_IO_COUNTER_BASE +
						((pdata->banks[bic.bank_idx].io_shift_bits + i) * 4));

		if (copy_to_user(argp, &bic, sizeof(bic)))
			return -EFAULT;
		break;

	case DIO_GET_BANK_INPUT_IO_FILTER:
		if (copy_from_user(&bio, argp, sizeof(bio)))
			return -EFAULT;

		if (pdata->version < 1)
			return -EPERM;

		if (!(bio.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!(bio.io_idx < pdata->banks[bio.bank_idx].nr_io))
			return -EINVAL;

		if (!pdata->banks[bio.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (bio.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = readl(priv->base + DIO_REG_BANK_IO_FILTER_BASE +
				((pdata->banks[bio.bank_idx].io_shift_bits + bio.io_idx) * 4));
		bio.value = reg;

		if (copy_to_user(argp, &bio, sizeof(bio)))
			return -EFAULT;
		break;

	case DIO_SET_BANK_INPUT_IO_FILTER:
		if (copy_from_user(&bio, argp, sizeof(bio)))
			return -EFAULT;

		if (pdata->version < 1)
			return -EPERM;

		if (!(bio.bank_idx < pdata->nr_bank))
			return -EINVAL;

		if (!(bio.io_idx < pdata->banks[bio.bank_idx].nr_io))
			return -EINVAL;

		if (!pdata->banks[bio.bank_idx].cap_input)
			return -EINVAL;

		if (!pdata->cap_samedirection)
			return -EPERM;

		if (bio.value) {
			if (bio.value < pdata->di_filter_min ||
				bio.value > pdata->di_filter_max)
				return -EINVAL;
		}

		ctrl = readl(priv->base + DIO_REG_BANK_CTRL_BASE + (bio.bank_idx * 4));
		if (ctrl & 1)
			return -EINVAL;

		reg = bio.value;

		writel(reg, priv->base + DIO_REG_BANK_IO_FILTER_BASE +
			((pdata->banks[bio.bank_idx].io_shift_bits + bio.io_idx) * 4));
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

#if KERNEL_VERSION(4, 16, 0) <= LINUX_VERSION_CODE
static __poll_t sdc_dio_poll(struct file *filp, poll_table *wait)
#else
static unsigned int sdc_dio_poll(struct file *filp, poll_table *wait)
#endif
{
	struct sdc_dio *priv = filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &priv->read_wait, wait);

	if (!kfifo_is_empty(&priv->fifo))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static irqreturn_t sdc_dio_irq_handler(int irq, void *dev_id)
{
	struct sdc_dio *priv = dev_id;
	struct dio_read r;

	r.input_delta = readl(priv->event);
	if (r.input_delta == 0)
		return IRQ_NONE;

	r.input = readl(priv->base + DIO_REG_INPUT);

	kfifo_in_spinlocked(&priv->fifo, &r, 1, &priv->fifo_lock);
	wake_up_interruptible(&priv->read_wait);
	return IRQ_HANDLED;
}

static const struct file_operations sdc_dio_fops = {
	.owner          = THIS_MODULE,
	.open           = sdc_dio_open,
	.release        = sdc_dio_release,
	.read           = sdc_dio_read,
	.unlocked_ioctl = sdc_dio_ioctl,
	.poll           = sdc_dio_poll,
#if KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE
	.llseek         = noop_llseek,
#else
	.llseek         = no_llseek,
#endif
};

#ifdef CONFIG_PROC_FS
static int sdc_dio_proc_show(struct seq_file *m, void *v)
{
	struct sdc_dio *priv = m->private;

	seq_printf(m, "pci_bus=%d;irq=%d;index=%d;pid=%d;\n",
		priv->pdata->board.pci_bus, priv->pdata->board.irq, priv->pdata->index,
		priv->pdev->id);

	return 0;
}

#if KERNEL_VERSION(4, 18, 0) > LINUX_VERSION_CODE
static int sdc_dio_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, sdc_dio_proc_show, PDE_DATA(inode));
}

static const struct file_operations sdc_dio_proc_fops = {
	.open		= sdc_dio_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static int sdc_dio_proc_create(struct sdc_dio *priv)
{
	char buf[16] = {0};

	if (!driver_ent)
		return -ENOMEM;

	sprintf(buf, "line%d", priv->minor);
#if KERNEL_VERSION(4, 18, 0) <= LINUX_VERSION_CODE
	priv->dev_ent =	proc_create_single_data(buf, 0444, driver_ent,
											sdc_dio_proc_show, priv);
#else
	priv->dev_ent = proc_create_data(buf, 0444, driver_ent,
											&sdc_dio_proc_fops, priv);
#endif
	if (!priv->dev_ent)
		return -ENOMEM;

	return 0;
}

static void sdc_dio_proc_remove(struct sdc_dio *priv)
{
	char buf[16] = {0};

	if (priv->dev_ent) {
		sprintf(buf, "line%d", priv->minor);
		remove_proc_entry(buf, driver_ent);
	}
}
#endif

#if KERNEL_VERSION(6, 2, 0) <= LINUX_VERSION_CODE
static int sdc_dio_uevent(const struct device *dev, struct kobj_uevent_env *env)
#else
static int sdc_dio_uevent(struct device *dev, struct kobj_uevent_env *env)
#endif
{
	add_uevent_var(env, "DEVMODE=%#o", 0666);
	return 0;
}

static int sdc_dio_probe(struct platform_device *pdev)
{
	struct sdc_dio_platdata *pdata;
	struct sdc_dio *priv;
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

	mutex_init(&priv->open_mutex);

	init_waitqueue_head(&priv->read_wait);
	spin_lock_init(&priv->fifo_lock);

	ret = kfifo_alloc(&priv->fifo, MAX_FIFOS, GFP_KERNEL);
	if (ret) {
		dev_err(&pdev->dev, "failed to alloc kfifo, err %d\n", ret);
		return ret;
	}

	ret = request_threaded_irq(priv->irq, sdc_dio_irq_handler, NULL,
							IRQF_SHARED, dev_name(&pdev->dev), priv);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to attach interrupt, err %d\n", ret);
		goto out_free_fifo;
	}

#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	ret = ida_alloc(&dev_nrs, GFP_KERNEL);
	if (ret < 0)
		goto out_free_irq;
	priv->minor = ret;
#else
	ret = ida_simple_get(&dev_nrs, 0, MAX_DEVS, GFP_KERNEL);
	if (ret < 0)
		goto out_free_irq;
	priv->minor = ret;
#endif

	cdev_init(&priv->cdev, &sdc_dio_fops);
	priv->cdev.owner = THIS_MODULE;
	ret = cdev_add(&priv->cdev, MKDEV(MAJOR(first_devt), priv->minor), 1);
	if (ret < 0)
		goto out_free_ida;

	dev = device_create(dev_class, NULL, MKDEV(MAJOR(first_devt),
					priv->minor), priv, "%s%d", DRV_NAME, priv->minor);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		dev_err(&pdev->dev, "failed to create class device, err %d\n", ret);
		goto out_cdev_del;
	}
	priv->dev = dev;

	dev_set_drvdata(priv->dev, priv);

#ifdef CONFIG_PROC_FS
	ret = sdc_dio_proc_create(priv);
	if (ret < 0)
		dev_warn(&pdev->dev, "failed to create device proc entry\n");
#endif

	return 0;

out_cdev_del:
	cdev_del(&priv->cdev);
out_free_ida:
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	ida_free(&dev_nrs, priv->minor);
#else
	ida_simple_remove(&dev_nrs, priv->minor);
#endif
out_free_irq:
	free_irq(priv->irq, priv);
out_free_fifo:
	kfifo_free(&priv->fifo);
	return ret;
}

#if KERNEL_VERSION(6, 11, 0) <= LINUX_VERSION_CODE
static void sdc_dio_remove(struct platform_device *pdev)
#else
static int sdc_dio_remove(struct platform_device *pdev)
#endif
{
	struct sdc_dio *priv = platform_get_drvdata(pdev);

#ifdef CONFIG_PROC_FS
	sdc_dio_proc_remove(priv);
#endif

	device_destroy(dev_class, MKDEV(MAJOR(first_devt), priv->minor));

	cdev_del(&priv->cdev);
#if KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE
	ida_free(&dev_nrs, priv->minor);
#else
	ida_simple_remove(&dev_nrs, priv->minor);
#endif
	free_irq(priv->irq, priv);
	kfifo_free(&priv->fifo);

#if KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE
	return 0;
#endif
}

#ifdef CONFIG_PM_SLEEP
static int sdc_dio_suspend(struct device *dev)
{
	struct sdc_dio *priv = dev_get_drvdata(dev);

	priv->reg_pm[2] = readl(priv->base + DIO_REG_INPUT_INVERT_ENABLE);
	priv->reg_pm[6] = readl(priv->base + DIO_REG_DI_COUNTER_INCREMENT_POSITIVE);
	priv->reg_pm[7] = readl(priv->base + DIO_REG_DI_COUNTER_INCREMENT_NEGATIVE);
	priv->reg_pm[8] = readl(priv->base + DIO_REG_DI_RISING_EVENT);
	priv->reg_pm[9] = readl(priv->base + DIO_REG_DI_FALLING_EVENT);
	priv->reg_pm[12] = readl(priv->base + DIO_REG_DIRECTION_CTRL);
	return 0;
}

static int sdc_dio_resume(struct device *dev)
{
	struct sdc_dio *priv = dev_get_drvdata(dev);

	writel(priv->reg_pm[2], priv->base + DIO_REG_INPUT_INVERT_ENABLE);
	writel(priv->reg_pm[6], priv->base + DIO_REG_DI_COUNTER_INCREMENT_POSITIVE);
	writel(priv->reg_pm[7], priv->base + DIO_REG_DI_COUNTER_INCREMENT_NEGATIVE);
	writel(priv->reg_pm[8], priv->base + DIO_REG_DI_RISING_EVENT);
	writel(priv->reg_pm[9], priv->base + DIO_REG_DI_FALLING_EVENT);
	writel(priv->reg_pm[12], priv->base + DIO_REG_DIRECTION_CTRL);
	return 0;
}
#endif

static const struct dev_pm_ops sdc_dio_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdc_dio_suspend, sdc_dio_resume)
};

static struct platform_driver sdc_dio_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &sdc_dio_pm_ops,
	},
	.probe = sdc_dio_probe,
	.remove = sdc_dio_remove,
};

static int __init sdc_dio_init(void)
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

	dev_class->dev_uevent = sdc_dio_uevent;

	ret = alloc_chrdev_region(&first_devt, 0, MAX_DEVS, DRV_NAME);
	if (ret < 0)
		goto out_destroy_class;

	ret = platform_driver_register(&sdc_dio_driver);
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
module_init(sdc_dio_init);

static void __exit sdc_dio_exit(void)
{
	platform_driver_unregister(&sdc_dio_driver);
	unregister_chrdev_region(first_devt, MAX_DEVS);
	class_destroy(dev_class);

#ifdef CONFIG_PROC_FS
	if (driver_ent)
		remove_proc_entry(DRV_NAME, NULL);
#endif
}
module_exit(sdc_dio_exit);

MODULE_AUTHOR("Jason Lee <jason_lee@sunix.com>");
MODULE_DESCRIPTION("SUNIX SDC DIO controller driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);

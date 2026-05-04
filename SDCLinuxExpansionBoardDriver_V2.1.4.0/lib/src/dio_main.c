// SPDX-License-Identifier: MIT
/*
 * SDC library
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

int dio_open(int line)
{
	struct dio_ex *ex = NULL;
	char buff[64];
	int ret, fd = -1;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	ex = dio_get_by_line(line);
	if (ex) {
		status = STATUS_DEVICE_BUSY;
		goto out;
	}

	snprintf(buff, sizeof(buff), "/dev/%s%d", SDC_DIO_DEV_NAME, line);
	fd = open(buff, O_RDWR | O_NONBLOCK);
	XPRINT("%s - open %s, fd:%d, errno:%d\n",
		__func__, buff, fd, fd < 0 ? errno : 0);
	if (fd < 0) {
		if (errno == EBUSY)
			status = STATUS_DEVICE_BUSY;
		else
			status = STATUS_NO_SUCH_DEVICE;
		goto out;
	}

	ex = dio_create_one(line, fd);
	if (!ex) {
		status = STATUS_ALLOC_MEMORY_FAIL;
		goto out_close;
	}

	memset(&ex->drvinfo, 0, sizeof(ex->drvinfo));
	ret = ioctl(ex->fd, DIO_GET_INFO, &ex->drvinfo);
	if (ret < 0) {
		status = STATUS_IOCTL_FAIL;
		goto out_terminate_one;
	}

	// Convert filter lower bound, min and max value from us, tick to ms
	if (ex->drvinfo.version >= 1) {
		switch (ex->drvinfo.di_sampling_freq) {
		default:
		case 500000:
			ex->di_filter_lower_bound_ms =
				ex->drvinfo.di_filter_lower_bound / 1000;
			ex->di_filter_min_ms =
				(ex->drvinfo.di_filter_min * 2) / 1000;
			ex->di_filter_max_ms =
				(ex->drvinfo.di_filter_max * 2) / 1000;
			break;

		case 2000:
			ex->di_filter_lower_bound_ms =
				ex->drvinfo.di_filter_lower_bound / 1000;
			ex->di_filter_min_ms =
				(ex->drvinfo.di_filter_min * 500) / 1000;
			ex->di_filter_max_ms =
				(ex->drvinfo.di_filter_max * 500) / 1000;
			break;
		}

	} else {
		ex->di_filter_lower_bound_ms = 0;
		ex->di_filter_min_ms = 0;
		ex->di_filter_max_ms = 0;
	}

	ret = pthread_mutex_init(&ex->callback_lock, NULL);
	if (ret < 0) {
		status = STATUS_CREATE_THREAD_FAIL;
		goto out_terminate_one;
	}
	ex->callback_bank_idx = -1;
	ex->callback_ptr = NULL;

	ret = dio_rxthd_create(ex);
	if (ret < 0) {
		status = STATUS_CREATE_THREAD_FAIL;
		goto out_destroy_mutex;
	}

out_destroy_mutex:
	if (status != STATUS_SUCCESS && ex)
		pthread_mutex_destroy(&ex->callback_lock);

out_terminate_one:
	if (status != STATUS_SUCCESS && ex)
		dio_terminate_one(ex->line);

out_close:
	if (status != STATUS_SUCCESS && fd > 0)
		close(fd);

out:
	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_close(int line)
{
	struct dio_ex *ex = NULL;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		dio_rxthd_terminate(ex);

		close(ex->fd);

		pthread_mutex_lock(&ex->callback_lock);
		ex->callback_bank_idx = -1;
		ex->callback_ptr = NULL;
		pthread_mutex_unlock(&ex->callback_lock);

		pthread_mutex_destroy(&ex->callback_lock);

		dio_terminate_one(ex->line);

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_get_info(int line, struct sdc_dio_info *info)
{
	struct dio_ex *ex = NULL;
	int i;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		if (!info) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		memset(info, 0, sizeof(*info));
		info->pci_bus = ex->drvinfo.pci_bus;
		info->irq = ex->drvinfo.irq;
		info->line = ex->drvinfo.line;
		info->index = ex->drvinfo.index;
		info->version = ex->drvinfo.version;
		info->cap_samedirection = ex->drvinfo.cap_samedirection;
		info->cap_storeflash = ex->drvinfo.cap_storeflash;
		info->nr_bank = ex->drvinfo.nr_bank;
		info->di_sampling_freq = ex->drvinfo.di_sampling_freq;
		info->di_filter_lower_bound_ms = ex->di_filter_lower_bound_ms;
		info->di_filter_min_ms = ex->di_filter_min_ms;
		info->di_filter_max_ms = ex->di_filter_max_ms;
		for (i = 0; i < info->nr_bank; i++) {
			info->banks[i].nr_port = ex->drvinfo.banks[i].nr_io;
			info->banks[i].cap_input = ex->drvinfo.banks[i].cap_input;
			info->banks[i].cap_output = ex->drvinfo.banks[i].cap_output;
			info->banks[i].cap_rising_trigger =
				ex->drvinfo.banks[i].cap_rising_trigger;
			info->banks[i].cap_falling_trigger =
				ex->drvinfo.banks[i].cap_falling_trigger;
		}

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_get_bank_direction(int line, int bank_idx, int *value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		if (!value) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		b.bank_idx = bank_idx;

		if (ioctl(ex->fd, DIO_GET_BANK_DIRECTION, &b) < 0) {
			status = STATUS_IOCTL_FAIL;
			break;
		}

		*value = b.value;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_set_bank_direction(int line, int bank_idx, int value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int temp = 0;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		temp = ex->drvinfo.banks[bank_idx].io_mask;
		temp = temp >> ex->drvinfo.banks[bank_idx].io_shift_bits;

		XPRINT("%s - temp:x%08X, value:x%08X\n", __func__, temp, value);

		if (ex->drvinfo.banks[bank_idx].cap_input &&
			!ex->drvinfo.banks[bank_idx].cap_output) {
			/* Bank capability is input only */
			if ((value & temp) != 0) {
				status = STATUS_DIO_BANK_NO_OUTPUT_CAP;
				break;
			}
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input &&
			ex->drvinfo.banks[bank_idx].cap_output) {
			/* Bank capability is output only */
			if ((value & temp) != temp) {
				status = STATUS_DIO_BANK_NO_INPUT_CAP;
				break;
			}
		}

		b.bank_idx = bank_idx;
		b.value = value;

		if (ioctl(ex->fd, DIO_SET_BANK_DIRECTION, &b) < 0)
			status = STATUS_IOCTL_FAIL;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_get_bank_state(int line, int bank_idx, int *value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		if (!value) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		b.bank_idx = bank_idx;

		if (ioctl(ex->fd, DIO_GET_BANK_STATE, &b) < 0) {
			status = STATUS_IOCTL_FAIL;
			break;
		}

		*value = b.value;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_set_bank_state(int line, int bank_idx, int value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_output) {
			status = STATUS_DIO_BANK_NO_OUTPUT_CAP;
			break;
		}

		b.bank_idx = bank_idx;
		b.value = value;

		if (ioctl(ex->fd, DIO_SET_BANK_STATE, &b) < 0)
			status = STATUS_IOCTL_FAIL;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_get_bank_input_invert(int line, int bank_idx, int *value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		if (!value) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		b.bank_idx = bank_idx;

		if (ioctl(ex->fd, DIO_GET_BANK_INPUT_INVERT, &b) < 0) {
			status = STATUS_IOCTL_FAIL;
			break;
		}

		*value = b.value;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_set_bank_input_invert(int line, int bank_idx, int value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		b.bank_idx = bank_idx;
		b.value = value;

		if (ioctl(ex->fd, DIO_SET_BANK_INPUT_INVERT, &b) < 0)
			status = STATUS_IOCTL_FAIL;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_get_bank_input_latch_positive_edge(int line, int bank_idx, int *value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		if (!value) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		b.bank_idx = bank_idx;

		if (ioctl(ex->fd, DIO_GET_BANK_INPUT_LATCH_POSITIVE, &b) < 0) {
			status = STATUS_IOCTL_FAIL;
			break;
		}

		*value = b.value;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_set_bank_input_latch_positive_edge(int line, int bank_idx, int value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		b.bank_idx = bank_idx;
		b.value = value;

		if (ioctl(ex->fd, DIO_SET_BANK_INPUT_LATCH_POSITIVE, &b) < 0)
			status = STATUS_IOCTL_FAIL;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_get_bank_input_latch_negative_edge(int line, int bank_idx, int *value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		if (!value) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		b.bank_idx = bank_idx;

		if (ioctl(ex->fd, DIO_GET_BANK_INPUT_LATCH_NEGATIVE, &b) < 0) {
			status = STATUS_IOCTL_FAIL;
			break;
		}

		*value = b.value;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_set_bank_input_latch_negative_edge(int line, int bank_idx, int value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		b.bank_idx = bank_idx;
		b.value = value;

		if (ioctl(ex->fd, DIO_SET_BANK_INPUT_LATCH_NEGATIVE, &b) < 0)
			status = STATUS_IOCTL_FAIL;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_get_bank_input_counter_increment_positive_edge(int line, int bank_idx, int *value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		if (!value) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		b.bank_idx = bank_idx;

		if (ioctl(ex->fd, DIO_GET_BANK_INPUT_COUNTER_INCREMENT_POSITIVE, &b) < 0) {
			status = STATUS_IOCTL_FAIL;
			break;
		}

		*value = b.value;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_set_bank_input_counter_increment_positive_edge(int line, int bank_idx, int value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		b.bank_idx = bank_idx;
		b.value = value;

		if (ioctl(ex->fd, DIO_SET_BANK_INPUT_COUNTER_INCREMENT_POSITIVE, &b) < 0)
			status = STATUS_IOCTL_FAIL;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_get_bank_input_counter_increment_negative_edge(int line, int bank_idx, int *value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		if (!value) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		b.bank_idx = bank_idx;

		if (ioctl(ex->fd, DIO_GET_BANK_INPUT_COUNTER_INCREMENT_NEGATIVE, &b) < 0) {
			status = STATUS_IOCTL_FAIL;
			break;
		}

		*value = b.value;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_set_bank_input_counter_increment_negative_edge(int line, int bank_idx, int value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		b.bank_idx = bank_idx;
		b.value = value;

		if (ioctl(ex->fd, DIO_SET_BANK_INPUT_COUNTER_INCREMENT_NEGATIVE, &b) < 0)
			status = STATUS_IOCTL_FAIL;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_get_bank_input_event_ctrl(int line, int bank_idx, int *rising, int *falling)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b1, b2;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		if (!rising || !falling) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		b1.bank_idx = bank_idx;
		b2.bank_idx = bank_idx;

		if (ioctl(ex->fd, DIO_GET_BANK_INPUT_EVENT_CTRL_RISING, &b1) < 0) {
			status = STATUS_IOCTL_FAIL;
			break;
		}

		if (ioctl(ex->fd, DIO_GET_BANK_INPUT_EVENT_CTRL_FALLING, &b2) < 0) {
			status = STATUS_IOCTL_FAIL;
			break;
		}

		*rising = b1.value;
		*falling = b2.value;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_set_bank_input_event_ctrl(int line, int bank_idx, int rising, int falling)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b1, b2;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		b1.bank_idx = bank_idx;
		b1.value = rising;
		b2.bank_idx = bank_idx;
		b2.value = falling;

		XPRINT("%s - b1.value:x%08X, b2.value:x%08X\n", __func__, b1.value, b2.value);

		if (ioctl(ex->fd, DIO_SET_BANK_INPUT_EVENT_CTRL_RISING, &b1) < 0) {
			status = STATUS_IOCTL_FAIL;
			break;
		}

		if (ioctl(ex->fd, DIO_SET_BANK_INPUT_EVENT_CTRL_FALLING, &b2) < 0)
			status = STATUS_IOCTL_FAIL;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_get_bank_output_initial_value(int line, int bank_idx, int *value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		if (!value) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_output) {
			status = STATUS_DIO_BANK_NO_OUTPUT_CAP;
			break;
		}

		b.bank_idx = bank_idx;

		if (ioctl(ex->fd, DIO_GET_BANK_OUTPUT_INITIAL_VALUE, &b) < 0) {
			status = STATUS_IOCTL_FAIL;
			break;
		}

		*value = b.value;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_set_bank_output_initial_value(int line, int bank_idx, int value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_output) {
			status = STATUS_DIO_BANK_NO_OUTPUT_CAP;
			break;
		}

		b.bank_idx = bank_idx;
		b.value = value;

		if (ioctl(ex->fd, DIO_SET_BANK_OUTPUT_INITIAL_VALUE, &b) < 0)
			status = STATUS_IOCTL_FAIL;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_set_bank_input_counter_reset(int line, int bank_idx, int port_idx)
{
	struct dio_ex *ex = NULL;
	struct dio_bank b;
	int value;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!(port_idx < ex->drvinfo.banks[bank_idx].nr_io)) {
			status = STATUS_DIO_BANK_PORT_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		value = 1 << port_idx;
		value = value << ex->drvinfo.banks[bank_idx].io_shift_bits;

		b.bank_idx = bank_idx;
		b.value = value;

		XPRINT("%s - b.bank_idx:%d, b.value:x%08X\n", __func__, b.bank_idx, b.value);

		if (ioctl(ex->fd, DIO_SET_BANK_INPUT_COUNTER_RESET, &b) < 0)
			status = STATUS_IOCTL_FAIL;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_get_bank_input_counter_value(int line, int bank_idx, int port_idx, int *value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank_input_counter bic;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		if (!value) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!(port_idx < ex->drvinfo.banks[bank_idx].nr_io)) {
			status = STATUS_DIO_BANK_PORT_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		bic.bank_idx = bank_idx;

		if (ioctl(ex->fd, DIO_GET_BANK_INPUT_COUNTER_VALUE, &bic) < 0) {
			status = STATUS_IOCTL_FAIL;
			break;
		}

		*value = bic.value[port_idx];

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_get_bank_input_filter_value(int line, int bank_idx, int port_idx, int *value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank_io bio;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		if (!value) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (ex->drvinfo.version < 1) {
			status = STATUS_CONTROLLER_VERSION_UNSUPPORT;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!(port_idx < ex->drvinfo.banks[bank_idx].nr_io)) {
			status = STATUS_DIO_BANK_PORT_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		bio.bank_idx = bank_idx;
		bio.io_idx = port_idx;

		if (ioctl(ex->fd, DIO_GET_BANK_INPUT_IO_FILTER, &bio) < 0) {
			status = STATUS_IOCTL_FAIL;
			break;
		}

		switch (ex->drvinfo.di_sampling_freq) {
		default:
		case 500000:
			*value = (bio.value * 2) / 1000;
			break;

		case 2000:
			*value = (bio.value * 500) / 1000;
			break;
		}

		XPRINT("%s - di_sampling_freq:%d, bio.value:x%08X,%d, *value:%d\n",
			__func__, ex->drvinfo.di_sampling_freq, bio.value, bio.value, *value);

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_set_bank_input_filter_value(int line, int bank_idx, int port_idx, int value)
{
	struct dio_ex *ex = NULL;
	struct dio_bank_io bio;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		if (ex->drvinfo.version < 1) {
			status = STATUS_CONTROLLER_VERSION_UNSUPPORT;
			break;
		}

		if (!(bank_idx < ex->drvinfo.nr_bank)) {
			status = STATUS_DIO_BANK_NOT_FOUND;
			break;
		}

		if (!(port_idx < ex->drvinfo.banks[bank_idx].nr_io)) {
			status = STATUS_DIO_BANK_PORT_NOT_FOUND;
			break;
		}

		if (!ex->drvinfo.banks[bank_idx].cap_input) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		if (value != 0) {
			if (value < ex->di_filter_min_ms || value > ex->di_filter_max_ms) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}
		}

		bio.bank_idx = bank_idx;
		bio.io_idx = port_idx;

		switch (ex->drvinfo.di_sampling_freq) {
		default:
		case 500000:
			bio.value = (value * 1000) / 2;
			break;

		case 2000:
			bio.value = (value * 1000) / 500;
			break;
		}

		XPRINT("%s - di_sampling_freq:%d, bio.value:x%08X,%d, value:%d\n",
			__func__, ex->drvinfo.di_sampling_freq, bio.value, bio.value, value);

		if (ioctl(ex->fd, DIO_SET_BANK_INPUT_IO_FILTER, &bio) < 0)
			status = STATUS_IOCTL_FAIL;

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_register_event_callback(int line, sdc_dio_callback callback_ptr)
{
	struct dio_ex *ex = NULL;
	int bank_idx = -1, i;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		if (!callback_ptr) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		for (i = 0; i < ex->drvinfo.nr_bank; i++) {
			if (ex->drvinfo.banks[i].cap_input) {
				bank_idx = i;
				break;
			}
		}
		if (bank_idx < 0) {
			status = STATUS_DIO_BANK_NO_INPUT_CAP;
			break;
		}

		pthread_mutex_lock(&ex->callback_lock);
		ex->callback_bank_idx = bank_idx;
		ex->callback_ptr = callback_ptr;
		pthread_mutex_unlock(&ex->callback_lock);

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

int dio_unregister_event_callback(int line)
{
	struct dio_ex *ex = NULL;
	int status = STATUS_SUCCESS;

	XPRINT("----> %s(), line:%d\n", __func__, line);

	do {
		ex = dio_get_by_line(line);
		if (!ex) {
			status = STATUS_NO_SUCH_DEVICE;
			break;
		}

		pthread_mutex_lock(&ex->callback_lock);
		ex->callback_bank_idx = -1;
		ex->callback_ptr = NULL;
		pthread_mutex_unlock(&ex->callback_lock);

	} while (0);

	XPRINT("<---- %s, status:%d\n", __func__, status);
	return status;
}

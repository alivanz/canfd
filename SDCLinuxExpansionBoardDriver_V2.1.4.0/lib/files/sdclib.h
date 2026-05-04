/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SUNIX SDC library define
 *
 * Copyright (C) 2021 - 2024, SUNIX Co., Ltd.
 *
 * Authors: Jason Lee <jason_lee@sunix.com>
 */

#ifndef _SDCLIB_DEFINE_H_
#define _SDCLIB_DEFINE_H_

#define SDC_DIO_BANK_MAX			32

struct sdc_dio_bank_info {
	int nr_port;
	int cap_input;
	int cap_output;
	int cap_rising_trigger;
	int cap_falling_trigger;
};

struct sdc_dio_info {
	int pci_bus;
	int irq;
	int line;
	int index;
	int version;
	int cap_samedirection;
	int cap_storeflash;
	int nr_bank;
	int di_sampling_freq;
	int di_filter_lower_bound_ms;
	int di_filter_min_ms;
	int di_filter_max_ms;
	struct sdc_dio_bank_info banks[SDC_DIO_BANK_MAX];
};

typedef void (*sdc_dio_callback)(int line, int bank_idx, int input, int input_delta);

int sdc_dio_open(int line);
int sdc_dio_close(int line);
int sdc_dio_get_info(int line, struct sdc_dio_info *info);
int sdc_dio_get_bank_direction(int line, int bank_idx, int *value);
int sdc_dio_set_bank_direction(int line, int bank_idx, int value);
int sdc_dio_get_bank_state(int line, int bank_idx, int *value);
int sdc_dio_set_bank_state(int line, int bank_idx, int value);
int sdc_dio_get_bank_input_invert(int line, int bank_idx, int *value);
int sdc_dio_set_bank_input_invert(int line, int bank_idx, int value);
int sdc_dio_get_bank_input_latch_positive_edge(int line, int bank_idx, int *value);
int sdc_dio_set_bank_input_latch_positive_edge(int line, int bank_idx, int value);
int sdc_dio_get_bank_input_latch_negative_edge(int line, int bank_idx, int *value);
int sdc_dio_set_bank_input_latch_negative_edge(int line, int bank_idx, int value);
int sdc_dio_get_bank_input_counter_increment_positive_edge(int line, int bank_idx, int *value);
int sdc_dio_set_bank_input_counter_increment_positive_edge(int line, int bank_idx, int value);
int sdc_dio_get_bank_input_counter_increment_negative_edge(int line, int bank_idx, int *value);
int sdc_dio_set_bank_input_counter_increment_negative_edge(int line, int bank_idx, int value);
int sdc_dio_get_bank_input_event_ctrl(int line, int bank_idx, int *rising, int *falling);
int sdc_dio_set_bank_input_event_ctrl(int line, int bank_idx, int rising, int falling);
int sdc_dio_get_bank_output_initial_value(int line, int bank_idx, int *value);
int sdc_dio_set_bank_output_initial_value(int line, int bank_idx, int value);
int sdc_dio_set_bank_input_counter_reset(int line, int bank_idx, int port_idx);
int sdc_dio_get_bank_input_counter_value(int line, int bank_idx, int port_idx, int *value);
int sdc_dio_get_bank_input_filter_value(int line, int bank_idx, int port_idx, int *value);
int sdc_dio_set_bank_input_filter_value(int line, int bank_idx, int port_idx, int value);

int sdc_dio_register_event_callback(int line, sdc_dio_callback callback_ptr);
int sdc_dio_unregister_event_callback(int line);

// return code define
#define STATUS_SUCCESS						0
#define STATUS_INVALID_PARAMETER			-1
#define STATUS_DEVICE_BUSY					-2
#define STATUS_NO_SUCH_DEVICE				-3
#define STATUS_ALLOC_MEMORY_FAIL			-4
#define STATUS_CREATE_THREAD_FAIL			-5
#define STATUS_IOCTL_FAIL					-6
#define STATUS_CONTROLLER_VERSION_UNSUPPORT	-7
#define STATUS_DIO_BANK_NOT_FOUND			-101
#define STATUS_DIO_BANK_PORT_NOT_FOUND		-102
#define STATUS_DIO_BANK_NO_INPUT_CAP		-103
#define STATUS_DIO_BANK_NO_OUTPUT_CAP		-104

#endif

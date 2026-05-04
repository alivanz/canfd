// SPDX-License-Identifier: MIT
/*
 * SDC library test program
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

static void _open(int *line)
{
	int temp;
	int status = STATUS_SUCCESS;

	if (*line >= 0) {
		printf("line(%d) open already\n", *line);
		return;
	}

	temp = input_dio_line();

	status = sdc_dio_open(temp);
	if (status == STATUS_SUCCESS)
		*line = temp;

	printf("----------------------------------------\n");
	printf("Open line(%d), status:%d\n", temp, status);
	printf("----------------------------------------\n");
}

static void _close(int *line)
{
	int temp;
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	temp = *line;

	status = sdc_dio_close(temp);
	if (status == STATUS_SUCCESS)
		*line = -1;

	printf("----------------------------------------\n");
	printf("Close line(%d), status:%d\n", temp, status);
	printf("----------------------------------------\n");
}

static void _get_info_and_print(int *line)
{
	struct sdc_dio_info info;
	int i;
	int status;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	status = sdc_dio_get_info(*line, &info);
	if (status != STATUS_SUCCESS) {
		printf("sdc_dio_get_info() fail, status:%d\n", status);
		return;
	}

	printf("----------------------------------------\n");
	printf("pci_bus                       : %d\n", info.pci_bus);
	printf("irq                           : %d\n", info.irq);
	printf("line                          : %d\n", info.line);
	printf("index                         : %d\n", info.index);
	printf("version                       : %d\n", info.version);
	printf("cap_samedirection             : %d\n", info.cap_samedirection);
	printf("cap_storeflash                : %d\n", info.cap_storeflash);
	printf("nr_bank                       : %d\n", info.nr_bank);
	printf("di_sampling_freq              : %d\n", info.di_sampling_freq);
	printf("di_filter_lower_bound_ms      : %d\n", info.di_filter_lower_bound_ms);
	printf("di_filter_min_ms              : %d\n", info.di_filter_min_ms);
	printf("di_filter_max_ms              : %d\n", info.di_filter_max_ms);
	for (i = 0; i < info.nr_bank; i++) {
		printf("======================================\n");
		printf("nr_port                       : %d\n", info.banks[i].nr_port);
		printf("cap_input                     : %d\n", info.banks[i].cap_input);
		printf("cap_output                    : %d\n", info.banks[i].cap_output);
		printf("cap_rising_trigger            : %d\n", info.banks[i].cap_rising_trigger);
		printf("cap_falling_trigger           : %d\n", info.banks[i].cap_falling_trigger);
	}
	if (info.nr_bank > 0)
		printf("======================================\n");
	printf("----------------------------------------\n");
}

static void _get_set_bank_direction(int *line)
{
	int bank_idx = 0;
	int value = 0;
	int op;
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	op = input_dio_get_or_set();
	bank_idx = input_dio_bank_index();
	if (op)
		value = input_u32_value();

	printf("\n");

	if (!op) {
		// get
		status = sdc_dio_get_bank_direction(*line, bank_idx, &value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_get_bank_direction() fail, status:%d\n", status);
			return;
		}

	} else {
		// set
		status = sdc_dio_set_bank_direction(*line, bank_idx, value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_set_bank_direction() fail, status:%d\n", status);
			return;
		}
	}

	printf("----------------------------------------\n");
	printf("%s direction ok\n", op ? "Set" : "Get");
	printf("BANK                          : %d\n", bank_idx);
	printf("Value                         : x%08X\n", value);
	printf("----------------------------------------\n");
}

static void _get_set_bank_state(int *line)
{
	int bank_idx = 0;
	int value = 0;
	int op;
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	op = input_dio_get_or_set();
	bank_idx = input_dio_bank_index();
	if (op)
		value = input_u32_value();

	printf("\n");

	if (!op) {
		// get
		status = sdc_dio_get_bank_state(*line, bank_idx, &value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_get_bank_state() fail, status:%d\n", status);
			return;
		}

	} else {
		// set
		status = sdc_dio_set_bank_state(*line, bank_idx, value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_set_bank_state() fail, status:%d\n", status);
			return;
		}
	}

	printf("----------------------------------------\n");
	printf("%s state ok\n", op ? "Set" : "Get");
	printf("BANK                          : %d\n", bank_idx);
	printf("Value                         : x%08X\n", value);
	printf("----------------------------------------\n");
}

static void _get_set_bank_input_invert(int *line)
{
	int bank_idx = 0;
	int value = 0;
	int op;
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	op = input_dio_get_or_set();
	bank_idx = input_dio_bank_index();
	if (op)
		value = input_u32_value();

	printf("\n");

	if (!op) {
		// get
		status = sdc_dio_get_bank_input_invert(*line, bank_idx, &value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_get_bank_input_invert() fail, status:%d\n", status);
			return;
		}

	} else {
		// set
		status = sdc_dio_set_bank_input_invert(*line, bank_idx, value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_set_bank_input_invert() fail, status:%d\n", status);
			return;
		}
	}

	printf("----------------------------------------\n");
	printf("%s input invert ok\n", op ? "Set" : "Get");
	printf("BANK                          : %d\n", bank_idx);
	printf("Value                         : x%08X\n", value);
	printf("----------------------------------------\n");
}

static void _get_set_bank_input_latch_positive_edge(int *line)
{
	int bank_idx = 0;
	int value = 0;
	int op;
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	op = input_dio_get_or_set();
	bank_idx = input_dio_bank_index();
	if (op)
		value = input_u32_value();

	printf("\n");

	if (!op) {
		// get
		status = sdc_dio_get_bank_input_latch_positive_edge(*line, bank_idx, &value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_get_bank_input_latch_positive_edge() fail, status:%d\n", status);
			return;
		}

	} else {
		// set
		status = sdc_dio_set_bank_input_latch_positive_edge(*line, bank_idx, value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_set_bank_input_latch_positive_edge() fail, status:%d\n", status);
			return;
		}
	}

	printf("----------------------------------------\n");
	printf("%s input latch positive edge ok\n", op ? "Set" : "Get");
	printf("BANK                          : %d\n", bank_idx);
	printf("Value                         : x%08X\n", value);
	printf("----------------------------------------\n");
}

static void _get_set_bank_input_latch_negative_edge(int *line)
{
	int bank_idx = 0;
	int value = 0;
	int op;
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	op = input_dio_get_or_set();
	bank_idx = input_dio_bank_index();
	if (op)
		value = input_u32_value();

	printf("\n");

	if (!op) {
		// get
		status = sdc_dio_get_bank_input_latch_negative_edge(*line, bank_idx, &value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_get_bank_input_latch_negative_edge() fail, status:%d\n", status);
			return;
		}

	} else {
		// set
		status = sdc_dio_set_bank_input_latch_negative_edge(*line, bank_idx, value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_set_bank_input_latch_negative_edge() fail, status:%d\n", status);
			return;
		}
	}

	printf("----------------------------------------\n");
	printf("%s input latch negative edge ok\n", op ? "Set" : "Get");
	printf("BANK                          : %d\n", bank_idx);
	printf("Value                         : x%08X\n", value);
	printf("----------------------------------------\n");
}

static void _get_set_bank_input_counter_increment_positive_edge(int *line)
{
	int bank_idx = 0;
	int value = 0;
	int op;
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	op = input_dio_get_or_set();
	bank_idx = input_dio_bank_index();
	if (op)
		value = input_u32_value();

	printf("\n");

	if (!op) {
		// get
		status = sdc_dio_get_bank_input_counter_increment_positive_edge(*line, bank_idx, &value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_get_bank_input_counter_increment_positive_edge() fail, status:%d\n", status);
			return;
		}

	} else {
		// set
		status = sdc_dio_set_bank_input_counter_increment_positive_edge(*line, bank_idx, value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_set_bank_input_counter_increment_positive_edge() fail, status:%d\n", status);
			return;
		}
	}

	printf("----------------------------------------\n");
	printf("%s input counter increment positive edge ok\n", op ? "Set" : "Get");
	printf("BANK                          : %d\n", bank_idx);
	printf("Value                         : x%08X\n", value);
	printf("----------------------------------------\n");
}

static void _get_set_bank_input_counter_increment_negative_edge(int *line)
{
	int bank_idx = 0;
	int value = 0;
	int op;
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	op = input_dio_get_or_set();
	bank_idx = input_dio_bank_index();
	if (op)
		value = input_u32_value();

	printf("\n");

	if (!op) {
		// get
		status = sdc_dio_get_bank_input_counter_increment_negative_edge(*line, bank_idx, &value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_get_bank_input_counter_increment_negative_edge() fail, status:%d\n", status);
			return;
		}

	} else {
		// set
		status = sdc_dio_set_bank_input_counter_increment_negative_edge(*line, bank_idx, value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_set_bank_input_counter_increment_negative_edge() fail, status:%d\n", status);
			return;
		}
	}

	printf("----------------------------------------\n");
	printf("%s input counter increment negative edge ok\n", op ? "Set" : "Get");
	printf("BANK                          : %d\n", bank_idx);
	printf("Value                         : x%08X\n", value);
	printf("----------------------------------------\n");
}

static void _get_set_bank_input_event_ctrl(int *line)
{
	int bank_idx = 0;
	int rising = 0, falling = 0;
	int op;
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	op = input_dio_get_or_set();
	bank_idx = input_dio_bank_index();
	if (op) {
		rising = input_dio_rising_event_ctrl();
		falling = input_dio_falling_event_ctrl();
	}

	printf("\n");

	if (!op) {
		// get
		status = sdc_dio_get_bank_input_event_ctrl(*line, bank_idx, &rising, &falling);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_get_bank_input_event_ctrl() fail, status:%d\n", status);
			return;
		}

	} else {
		// set
		status = sdc_dio_set_bank_input_event_ctrl(*line, bank_idx, rising, falling);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_set_bank_input_event_ctrl() fail, status:%d\n", status);
			return;
		}
	}

	printf("----------------------------------------\n");
	printf("%s input event ctrl ok\n", op ? "Set" : "Get");
	printf("RISING  - Value               : x%08X\n", rising);
	printf("FALLING - Value               : x%08X\n", falling);
	printf("----------------------------------------\n");
}

static void _get_set_bank_output_initial_value(int *line)
{
	int bank_idx = 0;
	int value = 0;
	int op;
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	op = input_dio_get_or_set();
	bank_idx = input_dio_bank_index();
	if (op)
		value = input_u32_value();

	printf("\n");

	if (!op) {
		// get
		status = sdc_dio_get_bank_output_initial_value(*line, bank_idx, &value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_get_bank_output_initial_value() fail, status:%d\n", status);
			return;
		}

	} else {
		// set
		status = sdc_dio_set_bank_output_initial_value(*line, bank_idx, value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_set_bank_output_initial_value() fail, status:%d\n", status);
			return;
		}
	}

	printf("----------------------------------------\n");
	printf("%s output initial value ok\n", op ? "Set" : "Get");
	printf("BANK                          : %d\n", bank_idx);
	printf("Value                         : x%08X\n", value);
	printf("----------------------------------------\n");
}

static void _set_bank_input_counter_reset(int *line)
{
	int bank_idx = 0, port_idx = 0;
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	bank_idx = input_dio_bank_index();
	port_idx = input_dio_bank_port_index();

	printf("\n");

	{
		// set
		status = sdc_dio_set_bank_input_counter_reset(*line, bank_idx, port_idx);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_set_bank_input_counter_reset() fail, status:%d\n", status);
			return;
		}
	}

	printf("----------------------------------------\n");
	printf("Set input counter reset ok\n");
	printf("BANK                          : %d\n", bank_idx);
	printf("PORT                          : %d\n", port_idx);
	printf("----------------------------------------\n");
}

static void _get_bank_input_counter_value(int *line)
{
	int bank_idx = 0, port_idx = 0;
	int value = 0;
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	bank_idx = input_dio_bank_index();
	port_idx = input_dio_bank_port_index();

	printf("\n");

	{
		// get
		status = sdc_dio_get_bank_input_counter_value(*line, bank_idx, port_idx, &value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_get_bank_input_counter_value() fail, status:%d\n", status);
			return;
		}
	}

	printf("----------------------------------------\n");
	printf("Get input counter value ok\n");
	printf("BANK                          : %d\n", bank_idx);
	printf("PORT                          : %d\n", port_idx);
	printf("Value                         : %d\n", value);
	printf("----------------------------------------\n");
}

static void _get_set_bank_input_port_filter_value(int *line)
{
	int bank_idx = 0, port_idx = 0;
	int value = 0;
	int op;
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	op = input_dio_get_or_set();
	bank_idx = input_dio_bank_index();
	port_idx = input_dio_bank_port_index();
	if (op)
		value = input_dio_filter_value();

	printf("\n");

	if (!op) {
		// get
		status = sdc_dio_get_bank_input_filter_value(*line, bank_idx, port_idx, &value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_get_bank_input_filter_value() fail, status:%d\n", status);
			return;
		}

	} else {
		// set
		status = sdc_dio_set_bank_input_filter_value(*line, bank_idx, port_idx, value);
		if (status != STATUS_SUCCESS) {
			printf("sdc_dio_set_bank_input_filter_value() fail, status:%d\n", status);
			return;
		}
	}

	printf("----------------------------------------\n");
	printf("%s input bank port filter ok\n", op ? "Set" : "Get");
	printf("BANK                          : %d\n", bank_idx);
	printf("PORT                          : %d\n", port_idx);
	printf("Value                         : %d\n", value);
	printf("----------------------------------------\n");
}

static void _event_callback_register(int *line)
{
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	printf("\n");

	status = sdc_dio_register_event_callback(*line, dio_callback);
	if (status != STATUS_SUCCESS) {
		printf("sdc_dio_register_event_callback() fail, status:%d\n", status);
		return;
	}

	printf("----------------------------------------\n");
	printf("event callback register ok\n");
	printf("----------------------------------------\n");
}

static void _event_callback_unregister(int *line)
{
	int status = STATUS_SUCCESS;

	if (*line < 0) {
		printf("line not open\n");
		return;
	}

	printf("\n");

	status = sdc_dio_unregister_event_callback(*line);
	if (status != STATUS_SUCCESS) {
		printf("sdc_dio_unregister_event_callback() fail, status:%d\n", status);
		return;
	}

	printf("----------------------------------------\n");
	printf("event callback unregister ok\n");
	printf("----------------------------------------\n");
}

static void _print_cmd(void)
{
	printf(">>------------------------------------<<\n");
	printf("     B : open\n");
	printf("     C : close\n");
	printf("     D : get and print info\n");
	printf("     F : get/set BANK direction\n");
	printf("     G : get/set BANK state\n");
	printf("     H : get/set BANK input invert\n");
	printf("     I : get/set BANK input latch positive edge\n");
	printf("     J : get/set BANK input latch negative edge\n");
	printf("     K : get/set BANK input counter increment positive edge\n");
	printf("     L : get/set BANK input counter increment negative edge\n");
	printf("     M : get/set BANK input event ctrl\n");
	printf("     N : get/set BANK output initial value\n");
	printf("     O : set BANK input counter reset\n");
	printf("     P : get BANK input counter value\n");
	printf("     R : get/set BANK input PORT filter value\n");
	printf("     8 : event callback register\n");
	printf("     9 : event callback unregister\n");
	printf("\n");
	printf("     Q : quit\n");
	printf(">>------------------------------------<<\n\n");
}

void dio_test(void)
{
	int cmd, line = -1;

	(void)!system("clear");
	_print_cmd();

	do {
		printf("\n>> CMD ?\n");
		cmd = sgetche();
		printf("\n");

		switch (cmd) {
		case 'b':
		case 'B':
			_open(&line);
			break;
		case 'c':
		case 'C':
			_close(&line);
			break;
		case 'd':
		case 'D':
			_get_info_and_print(&line);
			break;
		case 'f':
		case 'F':
			_get_set_bank_direction(&line);
			break;
		case 'g':
		case 'G':
			_get_set_bank_state(&line);
			break;
		case 'h':
		case 'H':
			_get_set_bank_input_invert(&line);
			break;
		case 'i':
		case 'I':
			_get_set_bank_input_latch_positive_edge(&line);
			break;
		case 'j':
		case 'J':
			_get_set_bank_input_latch_negative_edge(&line);
			break;
		case 'k':
		case 'K':
			_get_set_bank_input_counter_increment_positive_edge(&line);
			break;
		case 'l':
		case 'L':
			_get_set_bank_input_counter_increment_negative_edge(&line);
			break;
		case 'm':
		case 'M':
			_get_set_bank_input_event_ctrl(&line);
			break;
		case 'n':
		case 'N':
			_get_set_bank_output_initial_value(&line);
			break;
		case 'o':
		case 'O':
			_set_bank_input_counter_reset(&line);
			break;
		case 'p':
		case 'P':
			_get_bank_input_counter_value(&line);
			break;
		case 'r':
		case 'R':
			_get_set_bank_input_port_filter_value(&line);
			break;
		case '8':
			_event_callback_register(&line);
			break;
		case '9':
			_event_callback_unregister(&line);
			break;
		case '?':
			_print_cmd();
			break;
		default:
			break;
		}

	} while (cmd != 'q' && cmd != 'Q');

	if (line >= 0)
		_close(&line);
}

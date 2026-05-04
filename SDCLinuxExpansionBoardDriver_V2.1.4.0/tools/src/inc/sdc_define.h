/* SPDX-License-Identifier: MIT */
/*
 * SDC define
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

#ifndef _SDC_DEFINE_H_
#define _SDC_DEFINE_H_

#include <linux/types.h>
#include "xlist.h"

/*
 * sunix-sdc.h
 */
#define SDC_DEV_TYPE_CONFIG		0x00
#define SDC_DEV_TYPE_UART		0x01
#define SDC_DEV_TYPE_DIO		0x02
#define SDC_DEV_TYPE_SPI		0x03
#define SDC_DEV_TYPE_CAN		0x04
#define SDC_DEV_TYPE_PARPORT	0x05

#define SDC_SPI_DEVICE_FLASH	0x00
#define SDC_SPI_DEVICE_ADC		0x01
#define SDC_SPI_DEVICE_DAC		0x02
#define SDC_SPI_DEVICE_CAN		0x03

#define SDC_DIO_BANK_MAX		32
#define SDC_SPI_DEVICE_MAX		16

/*
 * sdc_config.h
 */
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

/*
 * sdc_dio.h
 */
#define DIO_BANK_IO_MAX			32
#define DIO_BANK_MAX			32

struct dio_bank {
	__u32 bank_idx; // _In_
	__u32 value; // _Inout_
};

struct dio_bank_input_counter {
	__u32 bank_idx; // _In_
	__u32 nr_io; // _Out_
	__u32 value[DIO_BANK_IO_MAX]; // _Out_
};

struct dio_bank_io {
	__u32 bank_idx; // _In_
	__u32 io_idx; // _In_
	__u32 value; // _Inout_
};

struct dio_read {
	__u32 input;
	__u32 input_delta;
};

struct dio_bank_info {
	int nr_io;
	int capability;
	int cap_input;
	int cap_output;
	int cap_rising_trigger;
	int cap_falling_trigger;
	int io_mask;
	int io_shift_bits;
};

struct dio_info {
	int pci_bus;
	int irq;
	int line;

	int index;
	int version;
	int capability;
	int cap_samedirection;
	int cap_storeflash;
	int nr_bank;

	int di_sampling_freq;
	int di_filter_lower_bound;
	int di_filter_min;
	int di_filter_max;

	struct dio_bank_info banks[DIO_BANK_MAX];
};

#define DIO_GET_INFO		_IOR('d', 1, struct dio_info *)
#define DIO_GET_BANK_DIRECTION \
							_IOWR('d', 3, struct dio_bank *)
#define DIO_SET_BANK_DIRECTION \
							_IOW('d', 4, struct dio_bank *)
#define DIO_GET_BANK_STATE	_IOWR('d', 5, struct dio_bank *)
#define DIO_SET_BANK_STATE	_IOW('d', 6, struct dio_bank *)
#define DIO_GET_BANK_INPUT_INVERT \
							_IOWR('d', 7, struct dio_bank *)
#define DIO_SET_BANK_INPUT_INVERT \
							_IOW('d', 8, struct dio_bank *)
#define DIO_GET_BANK_INPUT_LATCH_POSITIVE \
							_IOWR('d', 9, struct dio_bank *)
#define DIO_SET_BANK_INPUT_LATCH_POSITIVE \
							_IOW('d', 10, struct dio_bank *)
#define DIO_GET_BANK_INPUT_LATCH_NEGATIVE \
							_IOWR('d', 11, struct dio_bank *)
#define DIO_SET_BANK_INPUT_LATCH_NEGATIVE \
							_IOW('d', 12, struct dio_bank *)
#define DIO_GET_BANK_INPUT_COUNTER_INCREMENT_POSITIVE \
							_IOWR('d', 13, struct dio_bank *)
#define DIO_SET_BANK_INPUT_COUNTER_INCREMENT_POSITIVE \
							_IOW('d', 14, struct dio_bank *)
#define DIO_GET_BANK_INPUT_COUNTER_INCREMENT_NEGATIVE \
							_IOWR('d', 15, struct dio_bank *)
#define DIO_SET_BANK_INPUT_COUNTER_INCREMENT_NEGATIVE \
							_IOW('d', 16, struct dio_bank *)
#define DIO_GET_BANK_INPUT_EVENT_CTRL_RISING \
							_IOWR('d', 17, struct dio_bank *)
#define DIO_SET_BANK_INPUT_EVENT_CTRL_RISING \
							_IOW('d', 18, struct dio_bank *)
#define DIO_GET_BANK_INPUT_EVENT_CTRL_FALLING \
							_IOWR('d', 19, struct dio_bank *)
#define DIO_SET_BANK_INPUT_EVENT_CTRL_FALLING \
							_IOW('d', 20, struct dio_bank *)
#define DIO_GET_BANK_OUTPUT_INITIAL_VALUE \
							_IOWR('d', 21, struct dio_bank *)
#define DIO_SET_BANK_OUTPUT_INITIAL_VALUE \
							_IOW('d', 22, struct dio_bank *)
#define DIO_SET_BANK_INPUT_COUNTER_RESET \
							_IOW('d', 23, struct dio_bank *)
#define DIO_GET_BANK_INPUT_COUNTER_VALUE \
							_IOWR('d', 24, struct dio_bank_input_counter *)
#define DIO_GET_BANK_INPUT_IO_FILTER \
							_IOWR('d', 25, struct dio_bank_io *)
#define DIO_SET_BANK_INPUT_IO_FILTER \
							_IOW('d', 26, struct dio_bank_io *)

/*
 * config firmware header define
 */
struct firmware_h {
	unsigned char h_start_str[7];
	unsigned char h_version;
	unsigned char model_name[20];
	unsigned short major_version;
	unsigned short minor_version;
	unsigned short control_type;
	unsigned char date[6];
	unsigned char device_type_brand;
	unsigned char device_type_model;
	unsigned int firmware_data_size;
	unsigned char firmware_md5[16];
};

/*
 * other define
 */
#define SDC_UART_CFG_DIR		"/usr/share/sdc_8250"
#define SDC_UART_CFG_FILE		"/usr/share/sdc_8250/config"
#define SDC_UART_TEMP_FILE		"/usr/share/sdc_8250/temp"

#define SDC_CONFIG_DEV_NAME		"sdc_config"
#define SDC_UART_DEV_NAME		"ttyS"
#define SDC_DIO_DEV_NAME		"sdc_dio"
#define SDC_CAN_DEV_NAME		"can"
#define SDC_PARPORT_DEV_NAME	"parport"
#define SDC_GPIO_DEV_NAME		"gpiochip"

#define SDC_MFD_DRV_NAME		"sunix_sdc"
#define SDC_CONFIG_DRV_NAME		"sdc_config"
#define SDC_UART_DRV_NAME		"8250_sdc"
#define SDC_DIO_DRV_NAME		"sdc_dio"
#define SDC_SPI_DRV_NAME		"spi_sdc"
#define SDC_MCP251X_DRV_NAME	"sdc_mcp251x"
#define SDC_MCP251XFD_DRV_NAME	"sdc_mcp251xfd"
#define SDC_CAN_DRV_NAME		"can_sdc"
#define SDC_PARPORT_DRV_NAME	"parport_sdc"
#define SDC_GPIO_DRV_NAME		"sdc_gpio"

#define SDC_CHL_MAX				255
#define SDC_BOARD_MAX			255

#define BUFF_LENGTH				1024
#define NAME_LENGTH				64

struct sdc_config {
	__u32 mem_offset;
	__u32 mem_size;
	__u8 model;
	__u8 brand;
};

struct sdc_uart {
	__u32 io_offset;
	__u32 io_size;
	__u32 mem_offset;
	__u32 mem_size;
	__u16 tx_fifo_size;
	__u16 rx_fifo_size;
	__u32 clk_sig;
	__u8 clk_exp;
	__u32 capability;
};

struct sdc_dio_bank {
	__u8 nr_io;
	__u8 capability;

	/* extra */
	__u8 input_cap;
	__u8 output_cap;
	__u8 rising_trigger_cap;
	__u8 falling_trigger_cap;
	__u32 io_mask;
	__u32 io_shift_bits;
	__u32 io_direction;
};

struct sdc_dio {
	__u32 mem_offset;
	__u32 mem_size;
	__u8 nr_bank;
	__u8 capability;
	__u32 di_sampling_freq;
	__u32 di_filter_lower_bound;

	struct sdc_dio_bank banks[SDC_DIO_BANK_MAX];

	/* extra */
	__u8 is_shared;
	__u8 is_flash_store_cap;
	__u32 di_filter_min;
	__u32 di_filter_max;
};

struct sdc_spi_device {
	__u8 type;
	__u8 nr_gpio_input;
	__u8 nr_gpio_output;
	char name[17];

	/* extra */
	int can_line;
	char can_name[NAME_LENGTH];
	char can_device[NAME_LENGTH];
};

struct sdc_spi {
	__u32 mem_offset;
	__u32 mem_size;
	__u32 clk_sig;
	__u8 clk_exp;
	__u8 nr_device;
	struct sdc_spi_device devices[SDC_SPI_DEVICE_MAX];
};

struct sdc_can {
	__u32 mem_offset;
	__u32 mem_size;
	__u32 capability;
	__u16 tx_msg_queue_size;
	__u16 tx_event_queue_size;
	__u16 rx_msg_queue_size;
	__u16 nr_rx_filter_group;
	__u32 sysclk_sig;
	__u8 sysclk_exp;
	__u32 canrefclk_sig;
	__u8 canrefclk_exp;
};

struct sdc_parport {
	__u32 mem_offset;
	__u32 mem_size;
};

struct sdc_chl {
	__u8 index;
	__u8 type;
	__u8 version;
	__u8 cib_total_length;
	__u16 next_ptr;
	__u8 resource_cap;
	__u8 event_header_type;

	union {
		struct sdc_config config;
		struct sdc_uart uart;
		struct sdc_dio dio;
		struct sdc_spi spi;
		struct sdc_can can;
		struct sdc_parport parport;
	};

	/* extra */
	int supported;
	int pid;
	int line;
	char name[NAME_LENGTH];
	char device[NAME_LENGTH];
};

struct sdc_board {
	struct xlist entry;
	__u8 major_version;
	__u8 minor_version;
	__u8 nr_controller;
	__u8 dib_total_length;
	__u16 next_ptr;
	char model_name[17];
	struct sdc_chl chls[SDC_CHL_MAX];

	/* extra */
	int id;
	int error;
	int pci_bus;
	int irq;
};

#define SDC_BOARD_PTR(list) \
	CONTAIN_OF(list, struct sdc_board, entry)

#define ERR_MALLOC_MEMORY				-1
#define ERR_IOCTL_FAILURE				-2

#endif

// SPDX-License-Identifier: GPL-2.0
/*
 * can_sdc - SUNIX SDC CAN FD controller driver
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
// Added by Frank, 2024/06/06, add define
#if (KERNEL_VERSION(6, 8, 0) <= LINUX_VERSION_CODE)
#include <linux/platform_device.h>
#endif
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
#include <linux/bitfield.h>

#if KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE
#include <linux/unaligned.h>
#else
#include <asm/unaligned.h>
#endif

#include "../sunix-sdc.h"
#include "sdc_canfd.h"

#if defined(__x86_64__) || defined(amd64)
	/* NON-TEMPORAL STORE, CPU SUPPORT AVX2 OR SSE4_1 INSTRUCTION */
	#if defined(CPU_AVX2_ENABLE) || defined(CPU_SSE4_1_ENABLE)
		#define CPU_NTSTORE_AGLIGN_COPY_ENABLE	1
		#pragma GCC push_options
		#pragma GCC target("avx2", "sse4")

		// Added by Frank, 2024/06/11, add define
		// ignore "__SSE2__" block
		#if (KERNEL_VERSION(6, 8, 0) <= LINUX_VERSION_CODE)
			#pragma GCC target("no-sse2")
		#endif

		#define _MM_MALLOC_H_INCLUDED
		#include <smmintrin.h>
		#include <immintrin.h>
		#undef _MM_MALLOC_H_INCLUDED
		#pragma GCC pop_options
	#endif
#endif

/* CAN INIT AND EXIT DEBUG INFO */
#define CAN_DEBUG_INIT_AND_EXIT					0

/* DEVICE DEBUG FOR INFORMATION SHOW */
#define DEV_INFO_SHOW							0

/* DEVICE READ REGISTER FOR CHECK VALUE */
#define DEV_CHECK_REG_ENABLE					0

/* ENABLE GET CURRENT TIMESTAMP FROM REG */
#define GET_CURRENT_TIMESTAMP_ENABLE			1

/* ENABLE WORK WHEN SYS SUSPEND OR RESUME */
#define DEV_SLEEP_PM_OPS_WORK_ENABLE			1

/* RUNTIME POWER MANAGEMENT FUNC ENABLE */
#define DEV_RUNTIME_PM_FUNC_ENABLE				1

/* ENABLE CREATE FILE IN /PROC FOLDER */
#define PROC_FS_ENABLE							1

/* ENABLE LOCK EVENT VALUE */
#define EVENT_LOCK_ENABLE						1

/* TX LOCK ENABLE */
#define TX_LOCK_ENABLE							1

// Added by Andy
/* TERMINATION SET ENABLE */
#define TERMINATION_SET_ENABLE					1

/* OVERFLOW TEST */
#define TX_OVERFLOW_TEST_ENABLE					0
#define TEF_OVERFLOW_TEST_ENABLE				0
#define RX_OVERFLOW_TEST_ENABLE					0

#define DEBUG_RECORD_ENABLE						0
#define RX_MSG_TRI_POSI_ENABLE					1
#define RX_MSG_TIMEOUT_ENABLE					1
#define TEF_MSG_TRI_POSI_TEST_ENABLE			0

/* CAN DRIVER NAME */
#define DRV_NAME				"can_sdc"

/* NAPI WEIGHT */
#define SUNIX_NAPI_WEIGHT 32

/* OBJECT NUMBER DEFINIATION FOR CAN AND CAN FD */
#define SUNIX_TX_OBJ_NUM_CAN 8
#define SUNIX_TX_OBJ_NUM_CANFD 4

#if SUNIX_TX_OBJ_NUM_CAN > SUNIX_TX_OBJ_NUM_CANFD
#define SUNIX_TX_OBJ_NUM_MAX SUNIX_TX_OBJ_NUM_CAN
#else
#define SUNIX_TX_OBJ_NUM_MAX SUNIX_TX_OBJ_NUM_CANFD
#endif

/* QUEUE MESSAGE OVERFLOW FLAG */
#define SDC_CANFD_INT_TX_MSG_QUEUE_OVERFLOW		0
#define SDC_CANFD_INT_RX_MSG_QUEUE_OVERFLOW		1
#define SDC_CANFD_INT_TX_EVENT_QUEUE_OVERFLOW	2

// Added by Andy
// Change by Jason Lee, 20240627, base on https://www.kernel.org/doc/html/next/networking/can.html
#define SDC_CANFD_TERMINATION_EANBLED		120
#define SDC_CANFD_TERMINATION_DISABLED		CAN_TERMINATION_DISABLED

#if	TERMINATION_SET_ENABLE
const u16 termination_const[] = {SDC_CANFD_TERMINATION_DISABLED, SDC_CANFD_TERMINATION_EANBLED};
#endif

static const struct can_bittiming_const sdc_canfd_bittiming_const = {
	.name = DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 256,
	.tseg2_min = 1,
	.tseg2_max = 256,
	.sjw_max = 256,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 1,
};

static const struct can_bittiming_const sdc_canfd_data_bittiming_const = {
	.name = DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 64,
	.tseg2_min = 1,
	.tseg2_max = 32,
	.sjw_max = 32,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 1,
};

#ifdef CONFIG_PROC_FS
#if	PROC_FS_ENABLE
static struct proc_dir_entry *driver_ent;
#endif
#endif

#ifdef CPU_NTSTORE_AGLIGN_COPY_ENABLE
/* CPU need to support SSE4.1(128 bit) intructsion */
#pragma GCC target("avx2", "sse4")
static void copy16B_nts(void *dst, void *src)
{
	__m128i var128;

	kernel_fpu_begin();
	var128 = _mm_stream_load_si128((__m128i *)src);
	_mm_storeu_si128((__m128i *)dst, var128);
	smp_mb(); /* */
	kernel_fpu_end();
}

#ifdef CPU_AVX2_ENABLE
/* CPU need to support AVX2(256 bit) intructsion */
#pragma GCC target("avx2", "sse4")
static void copy32B_nts(void *dst, void *src)
{
	__m256i ymm0;

	kernel_fpu_begin();
	ymm0 = _mm256_stream_load_si256((const __m256i *)src);
	_mm256_storeu_si256((__m256i *)dst, ymm0);
	smp_mb(); /* */
	kernel_fpu_end();
}

#pragma GCC target("avx2", "sse4")
static void copy64B_nts(void *dst, void *src)
{
	__m256i ymm0, ymm1;

	kernel_fpu_begin();
	ymm0 = _mm256_stream_load_si256((const __m256i *)src);
	ymm1 = _mm256_stream_load_si256((const __m256i *)((uint8_t *)src + 32));
	_mm256_storeu_si256((__m256i *)dst, ymm0);
	_mm256_storeu_si256((__m256i *)((uint8_t *)dst + 32), ymm1);
	smp_mb(); /* */
	kernel_fpu_end();
}
#endif

static void memcpy_aligned_rx_tstore_16B(void *dst, void *src, int len)
{
#ifdef CPU_AVX2_ENABLE
	while (len >= 64) {
		copy64B_nts(dst, src);
		dst = (uint8_t *)dst + 64;
		src = (uint8_t *)src + 64;
		len -= 64;
	}
	while (len >= 32) {
		copy32B_nts(dst, src);
		dst = (uint8_t *)dst + 32;
		src = (uint8_t *)src + 32;
		len -= 32;
	}
#endif
#ifdef CPU_SSE4_1_ENABLE
	while (len >= 16) {
		copy16B_nts(dst, src);
		dst = (uint8_t *)dst + 16;
		src = (uint8_t *)src + 16;
		len -= 16;
	}
#endif
	if (len >= 8) {
		*(uint64_t *)dst = *(const uint64_t *)src;
		dst = (uint8_t *)dst + 8;
		src = (uint8_t *)src + 8;
		len -= 8;
	}
	if (len >= 4) {
		*(uint32_t *)dst = *(const uint32_t *)src;
		dst = (uint8_t *)dst + 4;
		src = (uint8_t *)src + 4;
		len -= 4;
	}
	if (len != 0) {
		dst = (uint8_t *)dst - (4 - len);
		src = (uint8_t *)src - (4 - len);
		*(uint32_t *)dst = *(const uint32_t *)src;
	}
}
#endif

static u32 sdc_canfd_read_reg(const struct sdc_canfd_priv *priv, int reg)
{
	return readl(priv->base + (reg * 4));
}

static int sdc_canfd_write_reg(const struct sdc_canfd_priv *priv, int reg,
						int val)
{
	writel(val, priv->base + (reg * 4));
	return 0;
}

static struct sdc_canfd_ops st_sdc_canfd_ops = {
	.read_reg = sdc_canfd_read_reg,
	.write_reg = sdc_canfd_write_reg,
};

#if	DEV_INFO_SHOW
static const char *sdc_canfd_get_mode_str(const u8 mode)
{
	switch (mode) {
	case SDC_CANFD_REG_CON_MODE_CONFIG:
		return "Configuration";
	case SDC_CANFD_REG_CON_MODE_NORMAL:
		return "Normal";
	case SDC_CANFD_REG_CON_MODE_LISTEN:
		return "Listen Only";
	case SDC_CANFD_REG_CON_MODE_INTERNAL_LOOPBACK:
		return "Internal Loopback";
	case SDC_CANFD_REG_CON_MODE_EXTERNAL_LOOPBACK:
		return "External Loopback";
	}

	return "<unknown>";
}
#endif

static int __sdc_canfd_chip_set_mode(const struct sdc_canfd_priv *priv,
						const u8 mode_req)
{
	u32 mode, val;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s(), mode_req:x%02x(%s)\n", __func__,
			mode_req, sdc_canfd_get_mode_str(mode_req));
#endif

	val = priv->ops->read_reg(priv, SDC_CANFD_REG_CON);
#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_CON:value = 0x%8x", val);
#endif

	val = FIELD_GET(GENMASK(31, 3), val);
#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_CON(bit31~3)value = 0x%8x", val);
#endif

	mode = FIELD_PREP(GENMASK(31, 3), val) | mode_req;

	priv->ops->write_reg(priv, SDC_CANFD_REG_CON, mode);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_CON);
	dev_info(priv->dev, "check, SDC_CANFD_REG_CON:value = 0x%8x", val);
	mode = FIELD_GET(SDC_CANFD_REG_CON_MODE_REGISTER_MASK, val);
	dev_info(priv->dev, "mode = 0x%8x(%s)", mode,
			sdc_canfd_get_mode_str(mode));
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

	return 0;
}

/* CAN set configuration mode */
static inline int sdc_canfd_chip_set_config_mode(
						const struct sdc_canfd_priv *priv,
						const u8 mode_req)
{
	return __sdc_canfd_chip_set_mode(priv, mode_req);
}

static inline int sdc_canfd_chip_set_oper_mode(
						const struct sdc_canfd_priv *priv)
{
	u8 mode_req;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s(), priv->can.ctrlmode:x%08x\n", __func__,
			priv->can.ctrlmode);
#endif
	/* listen, loopback or normal mode */
	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY) {
		mode_req = SDC_CANFD_REG_CON_MODE_LISTEN;
		dev_info(priv->dev, "CAN_CTRLMODE_LISTENONLY, mode_req:x%02x\n",
			mode_req);
	} else if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK) {
		mode_req = SDC_CANFD_REG_CON_MODE_INTERNAL_LOOPBACK;
	} else {
		mode_req = SDC_CANFD_REG_CON_MODE_NORMAL;
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "other as normal node\n");
#endif
	}

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done, mode:x%02x\n", __func__, mode_req);
#endif

	return __sdc_canfd_chip_set_mode(priv, mode_req);
}

static int sdc_canfd_chip_softreset(const struct sdc_canfd_priv *priv)
{
	int err;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

	/* config mode */
	err = sdc_canfd_chip_set_config_mode(priv, SDC_CANFD_REG_CON_MODE_CONFIG);
	if (err) {
		dev_err(priv->dev, "%s, err:%d\n", __func__, err);
		return err;
	}

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

	return 0;
}

/* CAN set nominal and data bit timing */
static int sdc_canfd_set_bittiming(const struct sdc_canfd_priv *priv)
{
	const struct can_bittiming *bt = &priv->can.bittiming;
#if KERNEL_VERSION(6, 16, 0) <= LINUX_VERSION_CODE
	const struct can_bittiming *dbt = &priv->can.fd.data_bittiming;
#else
	const struct can_bittiming *dbt = &priv->can.data_bittiming;
#endif
	u32 val = 0;

#if	DEV_INFO_SHOW
	u8 nbrp, ntsg1, ntsg2, nsjw;
	u8 dbrp, dtsg1, dtsg2, dsjw;
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

	/* Nominal Bit Time */
	val = FIELD_PREP(SDC_CANFD_REG_NOMINAL_BRP_MASK, bt->brp - 1) |
		FIELD_PREP(SDC_CANFD_REG_NOMINAL_TSEG1_MASK,
			   bt->prop_seg + bt->phase_seg1 - 1) |
		FIELD_PREP(SDC_CANFD_REG_NOMINAL_TSEG2_MASK,
			   bt->phase_seg2 - 1) |
		FIELD_PREP(SDC_CANFD_REG_NOMINAL_SJW_MASK, bt->sjw - 1);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "NBT, bt:%d, sp:%d, sjw:%d, tq:%d, prop_seg:%d, phase_seg1:%d, phase_seg2:%d, brp:%d\n",
			bt->bitrate, bt->sample_point, bt->sjw,
			bt->tq, bt->prop_seg, bt->phase_seg1, bt->phase_seg2, bt->brp);
#endif

	priv->ops->write_reg(priv, SDC_CANFD_REG_NOMINAL_BIT_TIME, val);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_NOMINAL_BIT_TIME);
	dev_info(priv->dev, "check, SDC_CANFD_REG_NOMINAL_BIT_TIME:value = 0x%8x", val);
	nbrp = FIELD_GET(SDC_CANFD_REG_NOMINAL_BRP_MASK, val);
	ntsg1 = FIELD_GET(SDC_CANFD_REG_NOMINAL_TSEG1_MASK, val);
	ntsg2 = FIELD_GET(SDC_CANFD_REG_NOMINAL_TSEG2_MASK, val);
	nsjw = FIELD_GET(SDC_CANFD_REG_NOMINAL_SJW_MASK, val);
	dev_info(priv->dev, "nbrp=0x%2x, ntsg1=0x%2x, ntsg2=0x%2x, nsjw=0x%2x",
			nbrp, ntsg1, ntsg2, nsjw);
#endif

	/* not support CAN FD control mode */
	if (!(priv->can.ctrlmode & CAN_CTRLMODE_FD)) {
		/*dev_info(priv->dev, "%s, not support CAN_CTRLMODE_FD\n", __func__); */
		return 0;
	}

	/* Data Bit Time */
	val = FIELD_PREP(SDC_CANFD_REG_DATA_BRP_MASK, dbt->brp - 1) |
		FIELD_PREP(SDC_CANFD_REG_DATA_TSEG1_MASK,
			   dbt->prop_seg + dbt->phase_seg1 - 1) |
		FIELD_PREP(SDC_CANFD_REG_DATA_TSEG2_MASK,
			   dbt->phase_seg2 - 1) |
		FIELD_PREP(SDC_CANFD_REG_DATA_SJW_MASK, dbt->sjw - 1);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "DBT, bt:%d, sp:%d, sjw:%d, tq:%d, prop_seg:%d, phase_seg1:%d, phase_seg2:%d, brp:%d\n",
			dbt->bitrate, dbt->sample_point, dbt->sjw,
			dbt->tq, dbt->prop_seg, dbt->phase_seg1, dbt->phase_seg2, dbt->brp);
#endif

	priv->ops->write_reg(priv, SDC_CANFD_REG_DATA_BIT_TIME, val);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_DATA_BIT_TIME);
	dev_info(priv->dev, "check, SDC_CANFD_REG_DATA_BIT_TIME:value = 0x%8x", val);
	dbrp = FIELD_GET(SDC_CANFD_REG_DATA_BRP_MASK, val);
	dtsg1 = FIELD_GET(SDC_CANFD_REG_DATA_TSEG1_MASK, val);
	dtsg2 = FIELD_GET(SDC_CANFD_REG_DATA_TSEG2_MASK, val);
	dsjw = FIELD_GET(SDC_CANFD_REG_DATA_SJW_MASK, val);
	dev_info(priv->dev, "dbrp=0x%2x, dtsg1=0x%2x, dtsg2=0x%2x, dsjw=0x%2x",
			dbrp, dtsg1, dtsg2, dsjw);
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s() done\n", __func__);
#endif

	return 0;
}

/* CAN set rx filter */
static int sdc_canfd_set_rx_filter(const struct sdc_canfd_priv *priv)
{
	u32 val = 0;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

	/* first filter(0), standard id*/
	priv->ops->write_reg(priv, SDC_CANFD_REG_FILTER_ID(0), val);
	priv->ops->write_reg(priv, SDC_CANFD_REG_FILTER_MASK(0), val);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_FILTER_ID0(0x%2x):value = 0x%8x",
			SDC_CANFD_REG_FILTER_ID(0), val);
	dev_info(priv->dev, "SDC_CANFD_REG_FILTER_MASK0(0x%2x):value = 0x%8x",
			SDC_CANFD_REG_FILTER_MASK(0), val);
#endif

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_FILTER_ID(0));
	dev_info(priv->dev, "check, SDC_CANFD_REG_FILTER_ID0(0x%2x):value = 0x%8x",
			SDC_CANFD_REG_FILTER_ID(0), val);
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_FILTER_MASK(0));
	dev_info(priv->dev, "check, SDC_CANFD_REG_FILTER_MASK0(0x%2x):value = 0x%8x",
			SDC_CANFD_REG_FILTER_MASK(0), val);
#endif

	/* second filter(1), extended id*/
	priv->ops->write_reg(priv, SDC_CANFD_REG_FILTER_ID(1),
			SDC_CANFD_REG_FILTER_EXT_ID_ENABLE);
	priv->ops->write_reg(priv, SDC_CANFD_REG_FILTER_MASK(1), val);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_FILTER_ID1(0x%2x):value = 0x%8x",
			SDC_CANFD_REG_FILTER_ID(1), val);
	dev_info(priv->dev, "SDC_CANFD_REG_FILTER_MASK1(0x%2x):value = 0x%8x",
			SDC_CANFD_REG_FILTER_MASK(1), val);
#endif

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_FILTER_ID(1));
	dev_info(priv->dev, "check, SDC_CANFD_REG_FILTER_ID1(0x%2x):value = 0x%8x",
			SDC_CANFD_REG_FILTER_ID(1), val);
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_FILTER_MASK(1));
	dev_info(priv->dev, "check, SDC_CANFD_REG_FILTER_MASK1(0x%2x):value = 0x%8x",
			SDC_CANFD_REG_FILTER_MASK(1), val);
#endif

	/* enable filter group, 2 filters */
	priv->ops->write_reg(priv, SDC_CANFD_REG_FILTER_CON, 0x03);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_FILTER_CON);
	dev_info(priv->dev, "check, SDC_CANFD_REG_FILTER_CON:value = 0x%8x", val);
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s() done\n", __func__);
#endif

	return 0;
}

/* CAN initial TX, RX msg and Tx event queue */
static int sdc_canfd_chip_queue_init(const struct sdc_canfd_priv *priv)
{
	u32 val = 0, tx_retrans = SDC_CANFD_REG_TX_MSG_RETRANS_ATTEMPT_UNLIMITED;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

	/* Tx message queue control */
	/* one shot */
	if (priv->can.ctrlmode & CAN_CTRLMODE_ONE_SHOT)
		tx_retrans = SDC_CANFD_REG_TX_MSG_RETRANS_ATTEMPT_DISABLE;

	val = SDC_CANFD_REG_TX_MSG_QUEUE_FLUSH_REQ |
		FIELD_PREP(SDC_CANFD_REG_TX_MSG_RETRANS_ATTEMPT_CON_MASK, tx_retrans);
#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_TX_MSG_QUEUE_CON:value = 0x%8x", val);
#endif

	priv->ops->write_reg(priv, SDC_CANFD_REG_TX_MSG_QUEUE_CON, val);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_TX_MSG_QUEUE_CON);
	dev_info(priv->dev, "check, SDC_CANFD_REG_TX_MSG_QUEUE_CON:value = 0x%8x", val);
#endif

	/* Rx message queue control */
	val = SDC_CANFD_REG_RX_MSG_QUEUE_FLUSH_REQ;

	/* Trigger postion:0 */
	val |= FIELD_PREP(SDC_CANFD_REG_RX_MSG_QUEUE_INT_TRIGGER_POS_MASK, 0);

#if RX_MSG_TRI_POSI_ENABLE
	val |= FIELD_PREP(SDC_CANFD_REG_RX_MSG_QUEUE_INT_TRIGGER_POS_MASK, 16);
#endif

#if RX_OVERFLOW_TEST_ENABLE
	/* Trigger on DW count */
	val |= SDC_CANFD_REG_RX_MSG_QUEUE_INT_TRIGGER_SOURCE;
	/* rx msg buffer size: 1024, set 1020 */
	val |= FIELD_PREP(SDC_CANFD_REG_RX_MSG_QUEUE_INT_TRIGGER_POS_MASK, 1020);
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_RX_MSG_QUEUE_CON:value = 0x%8x", val);
#endif
	priv->ops->write_reg(priv, SDC_CANFD_REG_RX_MSG_QUEUE_CON, val);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_RX_MSG_QUEUE_CON);
	dev_info(priv->dev, "check, SDC_CANFD_REG_RX_MSG_QUEUE_CON:value = 0x%8x", val);
#endif

	/* Tx event queue control */
	val = SDC_CANFD_REG_TX_EVENT_QUEUE_FLUSH_REQ;

	/* Trigger postion:0 */
	val |= FIELD_PREP(SDC_CANFD_REG_TX_EVENT_QUEUE_INT_TRIGGER_POS_MASK, 0);

#if TEF_OVERFLOW_TEST_ENABLE
	/* TEF msg buffer size: 1024 byte, supoort (1024/16)=64.. packet */
	val |= FIELD_PREP(SDC_CANFD_REG_TX_EVENT_QUEUE_INT_TRIGGER_POS_MASK, 62);
#endif

#if TEF_MSG_TRI_POSI_TEST_ENABLE
	val |= FIELD_PREP(SDC_CANFD_REG_TX_EVENT_QUEUE_INT_TRIGGER_POS_MASK, 4);
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_TX_EVENT_QUEUE_CON:value = 0x%8x", val);
#endif
	priv->ops->write_reg(priv, SDC_CANFD_REG_TX_EVENT_QUEUE_CON, val);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_TX_EVENT_QUEUE_CON);
	dev_info(priv->dev, "check, SDC_CANFD_REG_TX_EVENT_QUEUE_CON:value = 0x%8x", val);
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s() done\n", __func__);
#endif

	return 0;
}

/* CAN set timestamp counter */
static int sdc_canfd_chip_set_timestamp_counter(const struct sdc_canfd_priv *priv)
{
	u32 val = 0;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

	/* timestamp counter control */
	val = SDC_CANFD_REG_TIME_STAMP_COUNTER_ENABLE |
		FIELD_PREP(SDC_CANFD_REG_TIME_STAMP_CAPTURE_MASK,
			SDC_CANFD_REG_TIME_STAMP_CAPTURE_START_OF_FRAME) |
		FIELD_PREP(SDC_CANFD_REG_TIME_STAMP_PRE_SCALAR_MASK, 0xffff);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_TIME_STAMP_COUNTER_CON:value = 0x%8x", val);
#endif
	priv->ops->write_reg(priv, SDC_CANFD_REG_TIME_STAMP_COUNTER_CON, val);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_TIME_STAMP_COUNTER_CON);
	dev_info(priv->dev, "check, SDC_CANFD_REG_TIME_STAMP_COUNTER_CON:value = 0x%8x",
			val);
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s() done\n", __func__);
#endif

	return 0;
}

/* CAN set triple sample, termination, ISO-11898 mode, loopback and tx event queue control */
static int sdc_canfd_chip_set_basic_capa(const struct sdc_canfd_priv *priv)
{
	u32 val = 0, ctrl_val = 0, tx_transmit_interval = 0;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

	/* can control reg */
	/* 3 samples */
	if (priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		ctrl_val |= SDC_CANFD_REG_CON_TRIPLE_SAMPLE;
	/* FD no iso */
	if (!(priv->can.ctrlmode & CAN_CTRLMODE_FD_NON_ISO))
		ctrl_val |= SDC_CANFD_REG_CON_ISO11898_MODE;

	// Added by Andy
	/* termination, check from cib-capability value */
	/* if build-in termination resistor is supoort, set enable first,
	 * using do_set_termination callback to set is the flexible way
	 */
	if (priv->pdata->capability & 0x01) {
#if	TERMINATION_SET_ENABLE
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "can.termination = 0x%8x", priv->can.termination);
#endif
		if (priv->can.termination)
			ctrl_val |= SDC_CANFD_REG_CON_TERMINATION;
		else
			ctrl_val &= ~SDC_CANFD_REG_CON_TERMINATION;
#else
		ctrl_val |= SDC_CANFD_REG_CON_TERMINATION;
#endif
	}

	/* tx_transmit_interval:0 */
	val = ctrl_val | SDC_CANFD_REG_CON_MODE_CONFIG |
		SDC_CANFD_REG_CON_TX_EVENT_QUEUE |
		SDC_CANFD_REG_CON_TRANSMIT_ESI_GW_MODE |
		FIELD_PREP(SDC_CANFD_REG_CON_TX_TRANSMIT_INTERVAL_MASK,
			tx_transmit_interval);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_CON:value = 0x%8x", val);
#endif

	priv->ops->write_reg(priv, SDC_CANFD_REG_CON, val);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_CON);
	dev_info(priv->dev, "check, SDC_CANFD_REG_CON:value = 0x%8x", val);
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s() done\n", __func__);
#endif

	return 0;
}

/* CAN set interrupt enable */
static int sdc_canfd_chip_interrupts_enable(const struct sdc_canfd_priv *priv)
{
	u32 val = 0;

	/* interrupt enable control */
	/* tx msg queue overflow
	 * rx msg queue trigger pos, timeout and overflow
	 * tx event queue trigger pos and overflow
	 * message error detected
	 * bus state change
	 */
#if	RX_MSG_TRI_POSI_ENABLE
	val =
		SDC_CANFD_REG_INT_STATUS_TX_MSG_QUEUE_OVERFLOW |
		SDC_CANFD_REG_INT_STATUS_TX_EVENT_QUEUE_BELOW_TRIGGER_POS |
		SDC_CANFD_REG_INT_STATUS_TX_EVENT_QUEUE_OVERFLOW |
		SDC_CANFD_REG_INT_STATUS_RX_MSG_QUEUE_ABOVE_TRIGGER_POS |
		SDC_CANFD_REG_INT_STATUS_RX_MSG_QUEUE_OVERFLOW |
		SDC_CANFD_REG_INT_STATUS_MSG_ERROR_DETECTED |
		SDC_CANFD_REG_INT_STATUS_BUS_STATE_CHANGE_EVENT;

#if	RX_MSG_TIMEOUT_ENABLE
		val |= SDC_CANFD_REG_INT_STATUS_RX_MSG_QUEUE_TIMEOUT;
#endif

#else
	val =
		SDC_CANFD_REG_INT_STATUS_TX_MSG_QUEUE_OVERFLOW |
		SDC_CANFD_REG_INT_STATUS_TX_EVENT_QUEUE_BELOW_TRIGGER_POS |
		SDC_CANFD_REG_INT_STATUS_TX_EVENT_QUEUE_OVERFLOW |
		SDC_CANFD_REG_INT_STATUS_RX_MSG_QUEUE_ABOVE_TRIGGER_POS |
		SDC_CANFD_REG_INT_STATUS_RX_MSG_QUEUE_OVERFLOW |
		SDC_CANFD_REG_INT_STATUS_MSG_ERROR_DETECTED |
		SDC_CANFD_REG_INT_STATUS_BUS_STATE_CHANGE_EVENT;
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "enable, SDC_CANFD_REG_INT_ENABLE:value = 0x%8x", val);
#endif

	priv->ops->write_reg(priv, SDC_CANFD_REG_INT_ENABLE, val);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_INT_ENABLE);
	dev_info(priv->dev, "check, SDC_CANFD_REG_INT_ENABLE:value = 0x%8x", val);
#endif

	return 0;
}

/* CAN set interrupt disable */
static int sdc_canfd_chip_interrupts_disable(const struct sdc_canfd_priv *priv)
{
	u32 val = 0;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "disable, SDC_CANFD_REG_INT_ENABLE:value = 0x%8x", val);
#endif
	/* disable all interrupts */
	priv->ops->write_reg(priv, SDC_CANFD_REG_INT_ENABLE, val);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_INT_ENABLE);
	dev_info(priv->dev, "check, SDC_CANFD_REG_INT_ENABLE:value = 0x%8x", val);
#endif

	return 0;
}

/* CAN chip stop prodecure */
static int sdc_canfd_chip_stop(struct sdc_canfd_priv *priv,
						const enum can_state state)
{
	int err;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

	priv->can.state = state;

	sdc_canfd_chip_interrupts_disable(priv);

	err = sdc_canfd_chip_set_config_mode(priv, SDC_CANFD_REG_CON_MODE_CONFIG);
	if (err) {
		dev_err(priv->dev, "%s, err:%d\n", __func__, err);
		return err;
	}

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

	return 0;
}

/* CAN chip start prodecure */
static int sdc_canfd_chip_start(struct sdc_canfd_priv *priv)
{
	int err;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

	/* initial tx head and tail position */
	priv->tx_head = 0;
	priv->tx_tail = 0;

#if	DEBUG_RECORD_ENABLE
	priv->tx_count = 0;
	priv->rx_count = 0;
	priv->tx_div = 0;
	priv->rx_div = 0;
#endif

	err = sdc_canfd_chip_softreset(priv);
	if (err) {
		dev_err(priv->dev, "%s, err:%d\n", __func__, err);
		goto out_chip_stop;
	}

	err = sdc_canfd_set_bittiming(priv);
	if (err) {
		dev_err(priv->dev, "%s, err:%d\n", __func__, err);
		goto out_chip_stop;
	}

	err = sdc_canfd_chip_queue_init(priv);
	if (err) {
		dev_err(priv->dev, "%s, err:%d\n", __func__, err);
		goto out_chip_stop;
	}

	err = sdc_canfd_chip_set_timestamp_counter(priv);
	if (err) {
		dev_err(priv->dev, "%s, err:%d\n", __func__, err);
		goto out_chip_stop;
	}

	err = sdc_canfd_set_rx_filter(priv);
	if (err) {
		dev_err(priv->dev, "%s, err:%d\n", __func__, err);
		goto out_chip_stop;
	}

	err = sdc_canfd_chip_set_basic_capa(priv);
	if (err) {
		dev_err(priv->dev, "%s, err:%d\n", __func__, err);
		goto out_chip_stop;
	}

	//err = sdc_canfd_chip_interrupts_enable(priv);
	//if (err) {
		//pr_info("%s, err:%d\n", __func__, err);
	//}

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	err = sdc_canfd_chip_set_oper_mode(priv);
	if (err) {
		dev_err(priv->dev, "%s, err:%d\n", __func__, err);
		goto out_chip_stop;
	}

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

	return 0;

out_chip_stop:
	sdc_canfd_chip_stop(priv, CAN_STATE_STOPPED);
	dev_err(priv->dev, "%s, out_chip_stop, err:%d\n", __func__, err);
	return err;
}

/* CAN get error counter */
static int __sdc_canfd_get_berr_counter(const struct net_device *ndev,
					struct can_berr_counter *bec)
{
	const struct sdc_canfd_priv *priv = netdev_priv(ndev);
	u32 trec;

#if	DEV_INFO_SHOW
	netdev_info(ndev, "%s()\n", __func__);
#endif

	trec = priv->ops->read_reg(priv, SDC_CANFD_REG_TX_RX_ERROR_COUNT);

#if	DEV_INFO_SHOW
	netdev_info(ndev, "SDC_CANFD_REG_TX_RX_ERROR_COUNT, trec = 0x%8x", trec);
#endif

	bec->txerr = FIELD_GET(SDC_CANFD_REG_TX_ERROR_COUNT_MASK, trec);
	bec->rxerr = FIELD_GET(SDC_CANFD_REG_RX_ERROR_COUNT_MASK, trec);

#if	DEV_INFO_SHOW
	netdev_info(ndev, "%s done, txerr:%d, rxerr:%d\n", __func__,
			bec->txerr, bec->rxerr);
#endif

	return 0;
}

static int sdc_canfd_get_berr_counter(const struct net_device *ndev,
				      struct can_berr_counter *bec)
{
	const struct sdc_canfd_priv *priv = netdev_priv(ndev);
	int err;

#if	DEV_INFO_SHOW
	netdev_info(ndev, "%s()\n", __func__);
#endif

	/* Avoid waking up the controller if the interface is down  */
	if (!(ndev->flags & IFF_UP)) {
#if	DEV_INFO_SHOW
		netdev_info(ndev, "%s, check interface is down\n", __func__);
#endif
		return 0;
	}

	/* The controller is powered down during Bus Off, use saved
	 * bec values.
	 */
	if (priv->can.state == CAN_STATE_BUS_OFF) {
		*bec = priv->bec;
#if	DEV_INFO_SHOW
		netdev_info(ndev, "%s, state:CAN_STATE_BUS_OFF\n", __func__);
#endif
		return 0;
	}

	err = __sdc_canfd_get_berr_counter(ndev, bec);
	if (err) {
#if	DEV_INFO_SHOW
		netdev_info(ndev, "%s, err:%d\n", __func__, err);
#endif
		return err;
	}

#if	DEV_INFO_SHOW
	netdev_info(ndev, "%s done\n", __func__);
#endif

	return 0;
}

#if	GET_CURRENT_TIMESTAMP_ENABLE
/* CAN get current timestamp */
static inline int sdc_canfd_get_timestamp(const struct sdc_canfd_priv *priv,
					  u32 *timestamp)
{

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

	*timestamp = priv->ops->read_reg(priv, SDC_CANFD_REG_CURRENT_TIME_STAMP);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_CURRENT_TIME_STAMP, *timestamp:x%08x\n",
			*timestamp);
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

	return 0;
}
#endif

/* allocate CAN error socket buffer */
static struct sk_buff *
sdc_canfd_alloc_can_err_skb(const struct sdc_canfd_priv *priv,
			    struct can_frame **cf, u32 *timestamp)
{
	struct sk_buff *skb;
#if	GET_CURRENT_TIMESTAMP_ENABLE
	int err;
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

#if	GET_CURRENT_TIMESTAMP_ENABLE
	err = sdc_canfd_get_timestamp(priv, timestamp);
	if (err) {
		dev_err(priv->dev, "%s, err:%d\n", __func__, err);
		return NULL;
	}
#endif

	skb = alloc_can_err_skb(priv->ndev, cf);
	if (!skb) {
		netdev_err(priv->ndev, "%s, skb is NULL\n", __func__);
		return NULL;
	}

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done, skb:%p\n", __func__, skb);
#endif

	return skb;
}

/* CAN - handle bus change state interrupt */
static int sdc_canfd_handle_bus_state_change(struct sdc_canfd_priv *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct sk_buff *skb;
	struct can_frame *cf = NULL;
	struct can_berr_counter bec;
	struct net_device *ndev = priv->ndev;
	enum can_state new_state, rx_state, tx_state;
	u32 trec, txerr, rxerr, timestamp = 0;
	int err;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

	err = __sdc_canfd_get_berr_counter(ndev, &bec);
	if (err) {
#if	DEV_INFO_SHOW
		netdev_err(ndev, "%s, err:%d\n", __func__, err);
#endif
		return err;
	}

	/* read tx rx erorr count */
	trec = priv->ops->read_reg(priv, SDC_CANFD_REG_TX_RX_ERROR_COUNT);
	txerr = FIELD_GET(SDC_CANFD_REG_TX_ERROR_COUNT_MASK, trec);
	rxerr = FIELD_GET(SDC_CANFD_REG_RX_ERROR_COUNT_MASK, trec);
#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_TX_RX_ERROR_COUNT:trec = 0x%8x", trec);
	dev_info(priv->dev, "txerr = 0x%8x, rxerr = 0x%8x", txerr, rxerr);
#endif
	/* determine tx_state using txerr value */
	if (bec.txerr < 96)
		tx_state = CAN_STATE_ERROR_ACTIVE;
	else if (bec.txerr < 128)
		tx_state = CAN_STATE_ERROR_WARNING;
	else if (bec.txerr < 256)
		tx_state = CAN_STATE_ERROR_PASSIVE;
	else
		tx_state = CAN_STATE_BUS_OFF;
	/* determine rx_state using rxerr value */
	if (bec.rxerr < 96)
		rx_state = CAN_STATE_ERROR_ACTIVE;
	else if (bec.rxerr < 128)
		rx_state = CAN_STATE_ERROR_WARNING;
	else if (bec.rxerr < 256)
		rx_state = CAN_STATE_ERROR_PASSIVE;
	else
		rx_state = CAN_STATE_BUS_OFF;

	/* determine new state */
	new_state = max(tx_state, rx_state);
	dev_info(priv->dev, "tx_state:%d, rx_state:%d, new_state:%d\n",
			tx_state, rx_state, new_state);
	/* if current state is equal to new state, then return */
	if (new_state == priv->can.state) {
		dev_info(priv->dev, "%s, state equal\n", __func__);
		return 0;
	}

	/* The skb allocation might fail, but can_change_state()
	 * handles cf == NULL.
	 */
	skb = sdc_canfd_alloc_can_err_skb(priv, &cf, &timestamp);
	can_change_state(priv->ndev, cf, tx_state, rx_state);

	if (new_state == CAN_STATE_BUS_OFF) {
		dev_info(priv->dev, "new_state == CAN_STATE_BUS_OFF\n");
		/* As we're going to switch off the chip now, let's
		 * save the error counters and return them to
		 * userspace, if do_get_berr_counter() is called while
		 * the chip is in Bus Off.
		 */
		err = __sdc_canfd_get_berr_counter(ndev, &priv->bec);
		if (err) {
#if	DEV_INFO_SHOW
			netdev_err(ndev, "%s, err:%d\n", __func__, err);
#endif
			return err;
		}

		sdc_canfd_chip_stop(priv, CAN_STATE_BUS_OFF);
		can_bus_off(priv->ndev);
	}

	if (!skb) {
		netdev_info(ndev, "%s, skb is NULL\n", __func__);
		return 0;
	}

	if (new_state != CAN_STATE_BUS_OFF) {
		struct can_berr_counter bec;

#if	DEV_INFO_SHOW
		dev_info(priv->dev, "%s, new_state != CAN_STATE_BUS_OFF\n", __func__);
#endif
		err = sdc_canfd_get_berr_counter(priv->ndev, &bec);
		if (err) {
			netdev_err(ndev, "%s, err:%d\n", __func__, err);
			return err;
		}
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
		cf->can_id |= CAN_ERR_CNT;
#endif
		cf->data[6] = bec.txerr;
		cf->data[7] = bec.rxerr;
	}

	/* can rx offload queue work */
#if (KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE)
	err = can_rx_offload_queue_timestamp(&priv->offload, skb, timestamp);
#elif ((KERNEL_VERSION(4, 20, 0) <= LINUX_VERSION_CODE) && (KERNEL_VERSION(5, 19, 0) > LINUX_VERSION_CODE))
	err = can_rx_offload_queue_sorted(&priv->offload, skb, timestamp);
#elif ((KERNEL_VERSION(4, 14, 85) <= LINUX_VERSION_CODE) && (KERNEL_VERSION(4, 20, 0) > LINUX_VERSION_CODE))
	// ANDYJ
	err = can_rx_offload_queue_tail(&priv->offload, skb);
#else
	// ANDYJ
	err = can_rx_offload_irq_queue_err_skb(&priv->offload, skb);
#endif

	if (err)
		stats->rx_fifo_errors++;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

	return 0;
}

/* CAN - handle tx,rx msg and tx event queue overflow interrupt */
static int sdc_canfd_handle_ovif(struct sdc_canfd_priv *priv, int INT_flag)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct sk_buff *skb;
	struct can_frame *cf;
	u32 timestamp;
	u8 error_status;
	int err;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif
	/* tx message queue overflow */
	if (INT_flag == SDC_CANFD_INT_TX_MSG_QUEUE_OVERFLOW) {
		stats->tx_fifo_errors++;
		stats->tx_errors++;
		error_status = CAN_ERR_CRTL_TX_OVERFLOW;
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "%s(), got SDC_CANFD_INT_TX_MSG_QUEUE_OVERFLOW\n",
				__func__);
#endif
	/* rx message queue overflow */
	} else if (INT_flag == SDC_CANFD_INT_RX_MSG_QUEUE_OVERFLOW) {
		stats->rx_over_errors++;
		stats->rx_errors++;
		error_status = CAN_ERR_CRTL_RX_OVERFLOW;
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "%s(), got SDC_CANFD_INT_RX_MSG_QUEUE_OVERFLOW\n",
				__func__);
#endif
	/* tx event queue overflow */
	} else if (INT_flag == SDC_CANFD_INT_TX_EVENT_QUEUE_OVERFLOW) {
		stats->rx_errors++;
		error_status = CAN_ERR_CRTL_RX_OVERFLOW;
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "%s(), got SDC_CANFD_INT_TX_EVENT_QUEUE_OVERFLOW\n",
				__func__);
#endif
	}

	skb = sdc_canfd_alloc_can_err_skb(priv, &cf, &timestamp);
	if (!skb) {
		dev_err(priv->dev, "%s, skb is NULL\n", __func__);
		return 0;
	}

	cf->can_id |= CAN_ERR_CRTL;
	cf->data[1] = error_status;

	/* can rx offload queue work */
#if (KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE)
	err = can_rx_offload_queue_timestamp(&priv->offload, skb, timestamp);
#elif ((KERNEL_VERSION(4, 20, 0) <= LINUX_VERSION_CODE) && (KERNEL_VERSION(5, 19, 0) > LINUX_VERSION_CODE))
	err = can_rx_offload_queue_sorted(&priv->offload, skb, timestamp);
#elif ((KERNEL_VERSION(4, 14, 85) <= LINUX_VERSION_CODE) && (KERNEL_VERSION(4, 20, 0) > LINUX_VERSION_CODE))
	// ANDYJ
	err = can_rx_offload_queue_tail(&priv->offload, skb);
#else
	// ANDYJ
	err = can_rx_offload_irq_queue_err_skb(&priv->offload, skb);
#endif
	if (err)
		stats->rx_fifo_errors++;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

	return 0;
}

static int sdc_canfd_handle_error_detect(struct sdc_canfd_priv *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	u32 bdiag, timestamp = 0;
	struct sk_buff *skb;
	struct can_frame *cf = NULL;
	int err;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s\n", __func__);
#endif

#if	GET_CURRENT_TIMESTAMP_ENABLE
	err = sdc_canfd_get_timestamp(priv, &timestamp);
	if (err) {
		dev_err(priv->dev, "%s, err:%d\n", __func__, err);
		return err;
	}
#endif

	bdiag = priv->ops->read_reg(priv, SDC_CANFD_REG_BUS_DIAGNOSTIC);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_BUS_DIAGNOSTIC, bdiag:x%08x\n",
			bdiag);
#endif

	priv->can.can_stats.bus_error++;

	skb = alloc_can_err_skb(priv->ndev, &cf);
	if (cf)
		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

	/* RX errors */
	if (bdiag & SDC_CANFD_REG_BUS_DIAGNOSTIC_RECV_CANFD_MSG_ESI_FLAG_SET) {
#if	DEV_INFO_SHOW
		netdev_info(priv->ndev, "recv CANFD message esi set error\n");
#endif
		stats->rx_errors++;
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_UNSPEC;
	}

	if (bdiag & SDC_CANFD_REG_BUS_DIAGNOSTIC_RX_CRC_ERR) {
#if	DEV_INFO_SHOW
		netdev_info(priv->ndev, "CRC error\n");
#endif
		stats->rx_errors++;
		if (cf)
			cf->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ;
	}
	if (bdiag & SDC_CANFD_REG_BUS_DIAGNOSTIC_RX_STUFF_ERR) {
#if	DEV_INFO_SHOW
		netdev_info(priv->ndev, "Stuff error\n");
#endif
		stats->rx_errors++;
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_STUFF;
	}
	if (bdiag & SDC_CANFD_REG_BUS_DIAGNOSTIC_RX_FORM_ERR) {
#if	DEV_INFO_SHOW
		netdev_info(priv->ndev, "Format error\n");
#endif
		stats->rx_errors++;
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_FORM;
	}

	/* TX errors */
	if (bdiag & SDC_CANFD_REG_BUS_DIAGNOSTIC_TX_ACK_ERR) {
#if	DEV_INFO_SHOW
		netdev_info(priv->ndev, "ACK error\n");
#endif
		stats->tx_errors++;
		if (cf) {
			cf->can_id |= CAN_ERR_ACK;
			cf->data[2] |= CAN_ERR_PROT_TX;
		}
	}
	if (bdiag & SDC_CANFD_REG_BUS_DIAGNOSTIC_TX_BIT_ERR) {
#if	DEV_INFO_SHOW
		netdev_info(priv->ndev, "Bit error\n");
#endif
		stats->tx_errors++;
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_TX | CAN_ERR_PROT_BIT;
	}


	if (FIELD_GET(SDC_CANFD_REG_BUS_DIAGNOSTIC_BUS_STATE_MASK, bdiag) ==
		SDC_CANFD_REG_BUS_STATE_ERROR_ACTIVE) {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "GOT STATE ERROR_ACTIVE\n");
#endif
	} else if (FIELD_GET(SDC_CANFD_REG_BUS_DIAGNOSTIC_BUS_STATE_MASK, bdiag) ==
		SDC_CANFD_REG_BUS_STATE_ERROR_PASSIVE) {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "GOT STATE ERROR_PASSIVE\n");
#endif
	} else if (FIELD_GET(SDC_CANFD_REG_BUS_DIAGNOSTIC_BUS_STATE_MASK, bdiag) ==
		SDC_CANFD_REG_BUS_STATE_ERROR_BUS_OFF) {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "GOT STATE BUS_OFF\n");
#endif
	} else {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "UNKNOWN STATE\n");
#endif
	}

	if (!cf) {
		dev_info(priv->dev, "%s done, cf:NULL\n", __func__);
		return 0;
	}

	/* can rx offload queue work */
#if (KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE)
	err = can_rx_offload_queue_timestamp(&priv->offload, skb, timestamp);
#elif ((KERNEL_VERSION(4, 20, 0) <= LINUX_VERSION_CODE) && (KERNEL_VERSION(5, 19, 0) > LINUX_VERSION_CODE))
	err = can_rx_offload_queue_sorted(&priv->offload, skb, timestamp);
#elif ((KERNEL_VERSION(4, 14, 85) <= LINUX_VERSION_CODE) && (KERNEL_VERSION(4, 20, 0) > LINUX_VERSION_CODE))
	// ANDYJ
	err = can_rx_offload_queue_tail(&priv->offload, skb);
#else
	// ANDYJ
	err = can_rx_offload_irq_queue_err_skb(&priv->offload, skb);
#endif
	if (err)
		stats->rx_fifo_errors++;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

	return 0;
}

/* CAN - handle tx event queue not empty interrupt */
static int sdc_canfd_handle_tefif(struct sdc_canfd_priv *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	int i;
	u32 val, timestamp, tef_msg_count;
	/*u32 flags, id; */
	u8 tx_tail_pos;
#if	DEV_INFO_SHOW
	u8 dlc;
	u16 seq;
#endif

#if	TX_LOCK_ENABLE
	unsigned long tx_flags;
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif
	/* get tef message count */
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_TX_EVENT_QUEUE_STATUS);
	tef_msg_count = FIELD_GET(SDC_CANFD_REG_TX_EVENT_QUEUE_CURRENT_MSG_COUNT_MASK,
					val);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_TX_EVENT_QUEUE_STATUS, tef_msg_count:x%08x\n",
			tef_msg_count);
#endif
	/* read each tef messsge */
	for (i = 0; i < tef_msg_count; i++) {
		/* get flags, id and timestamp */
		/*flags = priv->ops->read_reg(priv, SDC_CANFD_REG_TX_EVENT_DW0);
		 *id = priv->ops->read_reg(priv, SDC_CANFD_REG_TX_EVENT_DW1);
		 */
		timestamp = priv->ops->read_reg(priv, SDC_CANFD_REG_TX_EVENT_DW2);

#if	DEV_INFO_SHOW
		dev_info(priv->dev, "tef msg pos:%d", i);
		/* get dlc, seq, ext id enable, rtr, brs and fdf */
		dlc = FIELD_GET(SDC_CANFD_REG_TX_EVENT_DLC_MASK, flags);
		seq = FIELD_GET(SDC_CANFD_REG_TX_EVENT_SEQ_NUM_MASK, flags);
		dev_info(priv->dev, "dlc = 0x%2x, seq: = 0x%8x", dlc, seq);
		if (flags & SDC_CANFD_REG_TX_EVENT_EXT_ID_ENABLE)
			dev_info(priv->dev, "TX_EVENT_EXT ENABLE");
		else
			dev_info(priv->dev, "TX_EVENT_EXT DISABLE");

		if (flags & SDC_CANFD_REG_TX_EVENT_RTR)
			dev_info(priv->dev, "TX_EVENT_RTR ENABLE");
		else
			dev_info(priv->dev, "TX_EVENT_RTR DISABLE");

		if (flags & SDC_CANFD_REG_TX_EVENT_BRS)
			dev_info(priv->dev, "TX_EVENT_BRS ENABLE");
		else
			dev_info(priv->dev, "TX_EVENT_BRS DISABLE");

		if (flags & SDC_CANFD_REG_TX_EVENT_ESI)
			dev_info(priv->dev, "TX_EVENT_ESI ENABLE");
		else
			dev_info(priv->dev, "TX_EVENT_ESI DISABLE");

		if (flags & SDC_CANFD_REG_TX_EVENT_FDF)
			dev_info(priv->dev, "TX_EVENT_FDF ENABLE");
		else
			dev_info(priv->dev, "TX_EVENT_FDF DISABLE");

		dev_info(priv->dev, "id = 0x%8x, timestamp: = 0x%8x", id, timestamp);
#endif

#if	TEF_OVERFLOW_TEST_ENABLE

#else

#if	TX_LOCK_ENABLE
		spin_lock_irqsave(&priv->tx_lock, tx_flags);
#endif

		tx_tail_pos = priv->tx_tail & (SUNIX_TX_OBJ_NUM_MAX - 1);
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "tx_tail = %u, tx_tail_pos = 0x%8x",
				priv->tx_tail, tx_tail_pos);
#endif
		stats->tx_bytes +=
		/* can rx offload queue work */
		// Added by Frank, 2024/06/06, add define
#if KERNEL_VERSION(6, 6, 0) <= LINUX_VERSION_CODE
		can_rx_offload_get_echo_skb_queue_timestamp(
						&priv->offload,
					    tx_tail_pos,
					    timestamp, NULL);
#elif KERNEL_VERSION(5, 12, 0) <= LINUX_VERSION_CODE
		can_rx_offload_get_echo_skb(&priv->offload,
					    tx_tail_pos,
					    timestamp, NULL);
#elif KERNEL_VERSION(4, 20, 0) <= LINUX_VERSION_CODE
		can_rx_offload_get_echo_skb(&priv->offload,
					    tx_tail_pos,
					    timestamp);
#else
		can_get_echo_skb(priv->ndev, tx_tail_pos);
#endif
		stats->tx_packets++;
#endif

#if	TEF_OVERFLOW_TEST_ENABLE
		/* decrease tef message count */
		/*
		 * priv->ops->write_reg(priv, SDC_CANFD_REG_TX_EVENT_QUEUE_CON,
		 *		SDC_CANFD_REG_TX_EVENT_QUEUE_DECREASE_MSG_COUNT |
		 *		FIELD_PREP(SDC_CANFD_REG_TX_EVENT_QUEUE_INT_TRIGGER_POS_MASK, 62));
		 * #if DEV_INFO_SHOW
		 * dev_info(priv->dev, "SDC_CANFD_REG_TX_EVENT_QUEUE_CON:value = 0x%8x",
		 *		(unsigned int)SDC_CANFD_REG_TX_EVENT_QUEUE_DECREASE_MSG_COUNT |
		 *		FIELD_PREP(SDC_CANFD_REG_TX_EVENT_QUEUE_INT_TRIGGER_POS_MASK, 62));
		 * #endif
		 */
#else
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "SDC_CANFD_REG_TX_EVENT_QUEUE_CON:value = 0x%8x",
				(unsigned int)SDC_CANFD_REG_TX_EVENT_QUEUE_DECREASE_MSG_COUNT);
#endif

#if	TEF_MSG_TRI_POSI_TEST_ENABLE
		/* decrease tef message count */
		priv->ops->write_reg(priv, SDC_CANFD_REG_TX_EVENT_QUEUE_CON,
				SDC_CANFD_REG_TX_EVENT_QUEUE_DECREASE_MSG_COUNT |
				FIELD_PREP(SDC_CANFD_REG_TX_EVENT_QUEUE_INT_TRIGGER_POS_MASK, 4));
#else
		/* decrease tef message count */
		priv->ops->write_reg(priv, SDC_CANFD_REG_TX_EVENT_QUEUE_CON,
				SDC_CANFD_REG_TX_EVENT_QUEUE_DECREASE_MSG_COUNT);
#endif
		priv->tx_tail++;

#if	DEV_INFO_SHOW
		dev_info(priv->dev, "increase tx_tail = %u", priv->tx_tail);
#endif

#if	TX_LOCK_ENABLE
		spin_unlock_irqrestore(&priv->tx_lock, tx_flags);
#endif

#endif

	}

#if	TEF_OVERFLOW_TEST_ENABLE

#else

#if	TX_LOCK_ENABLE
	spin_lock_irqsave(&priv->tx_lock, tx_flags);
#endif

	val = SUNIX_TX_OBJ_NUM_MAX - (priv->tx_head - priv->tx_tail);
#if	DEV_INFO_SHOW
	dev_info(priv->dev, "TEF, SUNIX_TX_OBJ_NUM_MAX(%d) - (priv->tx_head(%u) - priv->tx_tail(%u)) = %u\n",
			SUNIX_TX_OBJ_NUM_MAX, priv->tx_head, priv->tx_tail, val);
#endif
	if (SUNIX_TX_OBJ_NUM_MAX - (priv->tx_head - priv->tx_tail) > 0)
		netif_wake_queue(priv->ndev);

#if	TX_LOCK_ENABLE
	spin_unlock_irqrestore(&priv->tx_lock, tx_flags);
#endif

#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

	return 0;
}

/* CAN - read hareware rx object data */
static inline void
sdc_canfd_hw_rx_obj_read(const struct sdc_canfd_priv *priv,
		      struct sdc_canfd_hw_rx_obj_canfd *rx_obj)
{
	int i, j = 0;
	u8 len, dlc, do_ntstore = 0;

#ifdef CPU_NTSTORE_AGLIGN_COPY_ENABLE
	u8 data_buff[128];
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif
	/* get rx frame flags, id and timestamp */
	rx_obj->flags = priv->ops->read_reg(priv, SDC_CANFD_REG_RX_FRAME_CON0);
	rx_obj->id = priv->ops->read_reg(priv, SDC_CANFD_REG_RX_FRAME_CON1);
	rx_obj->ts = priv->ops->read_reg(priv, SDC_CANFD_REG_RX_FRAME_CON2);

	/* get dlc */
	dlc = FIELD_GET(SDC_CANFD_REG_RX_FRAME_DLC_MASK, rx_obj->flags);
#if	DEV_INFO_SHOW
	dev_info(priv->dev, "dlc:x%08x\n", dlc);
#endif
	/* CANFD, BRS and FDF */
	if (rx_obj->flags & SDC_CANFD_REG_RX_FRAME_FDF) {

#if KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE
		len = can_fd_dlc2len(dlc);
#else
		len = can_dlc2len(get_canfd_dlc(dlc));
#endif
	} else {

#if KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE
		/* due to not pass len to can_frame struct(CAN_CTRLMODE_CC_LEN8_DLC)
		 * so here not using can_frame_set_cc_len function
		 */
		len = can_cc_dlc2len(FIELD_GET(SDC_CANFD_REG_RX_FRAME_DLC_MASK,
						 rx_obj->flags));
		/*can_frame_set_cc_len((struct can_frame *)cfd, dlc,
		 *		priv->can.ctrlmode);
		 */
#else
		len = get_can_dlc(FIELD_GET(SDC_CANFD_REG_RX_FRAME_DLC_MASK,
						 rx_obj->flags));
#endif
	}

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "len:x%08x\n", len);
#endif

	if (len >= 16) {
#ifdef CPU_NTSTORE_AGLIGN_COPY_ENABLE
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "CPU_NTSTORE, len>=16, is:x%08x\n", len);
#endif
		do_ntstore = 1;
#endif
	}

	if (do_ntstore == 1) {
#ifdef CPU_NTSTORE_AGLIGN_COPY_ENABLE
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "do_ntstore->1\n");
#endif
		memcpy_aligned_rx_tstore_16B(&data_buff[0], priv->base + (0x50 * 4), len);
		for (i = 0; i < len; i++) {
			rx_obj->data[i] = data_buff[i];
#if	DEV_INFO_SHOW
			dev_info(priv->dev, "#%d, data_buff:x%08x, rx obj->data:x%08x\n", i,
				data_buff[i], rx_obj->data[i]);
#endif
		}
#endif
	} else {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "do_ntstore->0\n");
#endif
		/* len is determine by dlc */
		for (i = 0; i < len; i += 4) {
			u32 data = priv->ops->read_reg(priv, SDC_CANFD_REG_RX_MSG_DW(j));

#if	DEV_INFO_SHOW
			dev_info(priv->dev, "read x%08x, data:x%08x\n", SDC_CANFD_REG_RX_MSG_DW(j), data);
#endif
			j++;
			*(__le32 *)(rx_obj->data + i) = cpu_to_le32(data);

#if	DEV_INFO_SHOW
			dev_info(priv->dev, "#%d, rx obj->data:x%08x\n", i,
				*(__le32 *)(rx_obj->data + i));
#endif
		}
	}

#if	DEV_INFO_SHOW
	{
		dev_info(priv->dev, "--------------------------------------------\n");
		dev_info(priv->dev, "id    : x%08x\n", rx_obj->id);
		dev_info(priv->dev, "flags : x%08x\n", rx_obj->flags);
		dev_info(priv->dev, "ts    : x%08x\n", rx_obj->ts);
		dev_info(priv->dev, "data:\n");
		for (i = 0; i < len; i++)
			dev_info(priv->dev, "data[%d]:x%02x ", i, rx_obj->data[i]);
		dev_info(priv->dev, "--------------------------------------------\n");
	}
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

}

/* CAN - change hareware rx object data to socket buffer data */
static void sdc_canfd_hw_rx_obj_to_skb(const struct sdc_canfd_priv *priv,
			   const struct sdc_canfd_hw_rx_obj_canfd *rx_obj,
			   struct sk_buff *skb)
{
	struct canfd_frame *cfd = (struct canfd_frame *)skb->data;
	u8 dlc;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s\n", __func__);
#endif
	/* check ext id enable */
	if (rx_obj->flags & SDC_CANFD_REG_RX_FRAME_EXT_ID_ENABLE) {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "SDC_CANFD_REG_RX_FRAME_EXT_ID_ENABLE:YES\n");
#endif
		cfd->can_id = CAN_EFF_FLAG |
			FIELD_GET(SDC_CANFD_REG_RX_FRAME_EID_MASK, rx_obj->id);
	} else {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "SDC_CANFD_REG_RX_FRAME_EXT_ID_ENABLE:NO\n");
#endif
		cfd->can_id = FIELD_GET(SDC_CANFD_REG_RX_FRAME_SID_MASK, rx_obj->id);
	}

	/* get dlc */
	dlc = FIELD_GET(SDC_CANFD_REG_RX_FRAME_DLC_MASK, rx_obj->flags);

	/* CANFD, BRS and FDF */
	if (rx_obj->flags & SDC_CANFD_REG_RX_FRAME_FDF) {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "SDC_CANFD_REG_RX_FRAME_FDF:YES\n");
#endif
		if (rx_obj->flags & SDC_CANFD_REG_RX_FRAME_ESI) {
			cfd->flags |= CANFD_ESI;
#if	DEV_INFO_SHOW
			dev_info(priv->dev, "SDC_CANFD_REG_RX_FRAME_ESI:YES\n");
#endif
		}

		if (rx_obj->flags & SDC_CANFD_REG_RX_FRAME_BRS) {
			cfd->flags |= CANFD_BRS;
#if	DEV_INFO_SHOW
			dev_info(priv->dev, "SDC_CANFD_REG_RX_FRAME_BRS:YES\n");
#endif
		}

#if KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE
		cfd->len = can_fd_dlc2len(dlc);
#else
		cfd->len = can_dlc2len(get_canfd_dlc(dlc));
#endif
	} else {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "SDC_CANFD_REG_RX_FRAME_FDF:NO\n");
#endif
		if (rx_obj->flags & SDC_CANFD_REG_RX_FRAME_RTR) {
			cfd->can_id |= CAN_RTR_FLAG;
#if	DEV_INFO_SHOW
			dev_info(priv->dev, "SDC_CANFD_REG_RX_FRAME_RTR:YES\n");
#endif
		}

#if KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE
		//cfd->len = can_cc_dlc2len(FIELD_GET(SDC_CANFD_REG_RX_FRAME_DLC_MASK,
		//				 rx_obj->flags));
		can_frame_set_cc_len((struct can_frame *)cfd, dlc,
				     priv->can.ctrlmode);

#else
		cfd->len = get_can_dlc(FIELD_GET(SDC_CANFD_REG_RX_FRAME_DLC_MASK,
						 rx_obj->flags));
#endif
	}

	memcpy(cfd->data, rx_obj->data, cfd->len);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "--------------------------------------------\n");
	dev_info(priv->dev, "can_id : x%08x\n", cfd->can_id);
	dev_info(priv->dev, "len    : x%02x\n", cfd->len);
	dev_info(priv->dev, "flags  : x%02x\n", cfd->flags);
	dev_info(priv->dev, "data:\n");
	{
		int i;

		for (i = 0; i < cfd->len; i++)
			dev_info(priv->dev, "x%02x ", cfd->data[i]);
	}
	dev_info(priv->dev, "--------------------------------------------\n");

	dev_info(priv->dev, "%s done\n", __func__);
#endif
}

/* CAN - handle each rx msg  */
static int
sdc_canfd_handle_rxif_one(struct sdc_canfd_priv *priv,
			  const struct sdc_canfd_hw_rx_obj_canfd *rx_obj)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct sk_buff *skb;
	struct canfd_frame *cfd;
	int err;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

	/* CAN FD - FDF */
	if (rx_obj->flags & SDC_CANFD_REG_RX_FRAME_FDF)
		skb = alloc_canfd_skb(priv->ndev, &cfd);
	else
		skb = alloc_can_skb(priv->ndev, (struct can_frame **)&cfd);

	/* skb is NULL, dropped rx packet */
	if (!skb) {
		stats->rx_dropped++;
		netdev_info(priv->ndev, "%s, skb:NULL\n", __func__);
		return 0;
	}

	sdc_canfd_hw_rx_obj_to_skb(priv, rx_obj, skb);


	/* can rx offload queue work */
#if (KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE)
	err = can_rx_offload_queue_timestamp(&priv->offload, skb, rx_obj->ts);
#elif ((KERNEL_VERSION(4, 20, 0) <= LINUX_VERSION_CODE) && (KERNEL_VERSION(5, 19, 0) > LINUX_VERSION_CODE))
	err = can_rx_offload_queue_sorted(&priv->offload, skb, rx_obj->ts);
#elif ((KERNEL_VERSION(4, 14, 85) <= LINUX_VERSION_CODE) && (KERNEL_VERSION(4, 20, 0) > LINUX_VERSION_CODE))
	// ANDYJ
	err = can_rx_offload_queue_tail(&priv->offload, skb);
#else
	// ANDYJ
	err = can_rx_offload_irq_queue_err_skb(&priv->offload, skb);
#endif
	if (err)
		stats->rx_fifo_errors++;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_RX_MSG_CON:value = 0x%8x",
			(unsigned int)SDC_CANFD_REG_RX_MSG_DECREASE_MSG_COUNT);
#endif

	/* decrease rx message count */
	priv->ops->write_reg(priv, SDC_CANFD_REG_RX_MSG_CON,
				SDC_CANFD_REG_RX_MSG_DECREASE_MSG_COUNT);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

	return 0;
}

/* CAN - handle rx msg queue not empty interrupt */
static int sdc_canfd_handle_rxif(struct sdc_canfd_priv *priv)
{
	int i, err;
	struct sdc_canfd_hw_rx_obj_canfd rx_obj;
	u32 val, rx_msg_count;

#if	DEV_INFO_SHOW
	u32 rx_dw_count;

	dev_info(priv->dev, "%s()\n", __func__);
#endif

	val = priv->ops->read_reg(priv, SDC_CANFD_REG_RX_MSG_QUEUE_STATUS);

	rx_msg_count = FIELD_GET(SDC_CANFD_REG_RX_MSG_QUEUE_CURRENT_MSG_COUNT_MASK, val);

#if	DEV_INFO_SHOW
	rx_dw_count = FIELD_GET(SDC_CANFD_REG_RX_MSG_QUEUE_CURRENT_DW_COUNT_MASK, val);
	dev_info(priv->dev, "SDC_CANFD_REG_RX_MSG_QUEUE_STATUS, val:x%08x, rx_msg_count:x%08x, rx_dw_count:x%08x\n",
			val, rx_msg_count, rx_dw_count);
#endif
	/* read each rx messsge */
	for (i = 0; i < rx_msg_count; i++) {
		sdc_canfd_hw_rx_obj_read(priv, &rx_obj);
		err = sdc_canfd_handle_rxif_one(priv, &rx_obj);
		if (err) {
			dev_err(priv->dev, "%s, err:%d\n", __func__, err);
			return err;
		}
	}

#if DEBUG_RECORD_ENABLE
	priv->rx_count += rx_msg_count;
	val = priv->rx_count / 1000000;
	if (val != priv->rx_div) {
		priv->rx_div = val;
		dev_info(priv->dev, "rx_count:%lu\n", priv->rx_count);
	}
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

	return 0;
}

/* CAN - bottom half - irq routine, handle interrupt */
static irqreturn_t sdc_canfd_thread_fn(int irq, void *dev_id)
{
#if DEBUG_RECORD_ENABLE
	int do_count = 0;
#endif
	int err;
	struct sdc_canfd_priv *priv = dev_id;
	irqreturn_t handled = IRQ_NONE;
	unsigned int event;
#if EVENT_LOCK_ENABLE
	unsigned long flags;
#endif

	u32 interrupt_status, interrupt_enable;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s(), irq:%d, name:%s\n", __func__,
			irq, priv->ndev->name);
#endif

#if	DEV_INFO_SHOW
	u32 trec;
	int txerr, rxerr;
#endif

#if EVENT_LOCK_ENABLE
	spin_lock_irqsave(&priv->event_lock, flags);
	event = priv->event_value;
	priv->event_value = 0;
	spin_unlock_irqrestore(&priv->event_lock, flags);
#else
	event = priv->event_value;
	priv->event_value = 0;
#endif

	if (event == 0 || event == 1) {
#if KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE
		can_rx_offload_threaded_irq_finish(&priv->offload);
#endif
		return handled;
	}

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s(), name:%s, event_value:0x%4x\n", __func__,
		priv->ndev->name, event);
#endif

	interrupt_status = event;

	interrupt_enable = priv->ops->read_reg(priv, SDC_CANFD_REG_INT_ENABLE);

	do {

		u32 intf_pending;

#if	DEV_INFO_SHOW
		dev_info(priv->dev, "%s, interrupt_enable:0x%08x, interrupt_status(event):0x%08x\n",
				__func__, interrupt_enable, interrupt_status);
#endif
		intf_pending = FIELD_GET(SDC_CANFD_REG_INT_STATUS_ALL_MASK, interrupt_status) &
					FIELD_GET(SDC_CANFD_REG_INT_ENABLE_ALL_MASK, interrupt_enable);
#if	DEV_INFO_SHOW
		if (intf_pending > 0)
			dev_info(priv->dev, "%s, intf_pending:0x%08x\n",
				__func__, intf_pending);
#endif

#if	DEV_INFO_SHOW
		trec = priv->ops->read_reg(priv, SDC_CANFD_REG_TX_RX_ERROR_COUNT);
		if (trec > 0) {
			txerr = FIELD_GET(SDC_CANFD_REG_TX_ERROR_COUNT_MASK, trec);
			rxerr = FIELD_GET(SDC_CANFD_REG_RX_ERROR_COUNT_MASK, trec);
			dev_info(priv->dev, "SDC_CANFD_REG_TX_RX_ERROR_COUNT, trec = 0x%8x, txerr:%d, rxerr:%d",
					trec, txerr, rxerr);
		}
#endif

		if (!(intf_pending)) {
#if KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE
			can_rx_offload_threaded_irq_finish(&priv->offload);
#endif

#if	DEV_INFO_SHOW
			dev_info(priv->dev, "%s done, no INT pending, do_count:%d, handled:%d\n",
					__func__, do_count, handled);
#endif

#if DEBUG_RECORD_ENABLE
			if (do_count >= 10) {
				dev_info_ratelimited(priv->dev, "%s, exit, do_count >= 10, is:%d\n",
					__func__, do_count);
			}
#endif
			return handled;
		}

		/* check status for tx message queue overflow */
		if (intf_pending & SDC_CANFD_REG_INT_STATUS_TX_MSG_QUEUE_OVERFLOW) {
			dev_info_ratelimited(priv->dev, "%s, got SDC_CANFD_REG_INT_STATUS_TX_MSG_QUEUE_OVERFLOW\n",
					__func__);
			err = sdc_canfd_handle_ovif(priv, SDC_CANFD_INT_TX_MSG_QUEUE_OVERFLOW);
			if (err) {
				dev_err(priv->dev, "%s, err:%d\n", __func__, err);
				goto out_fail;
			}
		}

		/* check status for tx event queue trigger postion */
		if (intf_pending & SDC_CANFD_REG_INT_STATUS_TX_EVENT_QUEUE_BELOW_TRIGGER_POS) {
#if	DEV_INFO_SHOW
			dev_info(priv->dev, "%s, got SDC_CANFD_REG_INT_STATUS_TX_EVENT_QUEUE_BELOW_TRIGGER_POS\n",
					__func__);
#endif
			err = sdc_canfd_handle_tefif(priv);
			if (err) {
				dev_err(priv->dev, "%s, err:%d\n", __func__, err);
				goto out_fail;
			}
		}

		/* check status for tx event queue overflow */
		if (intf_pending & SDC_CANFD_REG_INT_STATUS_TX_EVENT_QUEUE_OVERFLOW) {
			dev_info_ratelimited(priv->dev, "%s, got SDC_CANFD_REG_INT_STATUS_TX_EVENT_QUEUE_OVERFLOW\n",
					__func__);
			err = sdc_canfd_handle_ovif(priv, SDC_CANFD_INT_TX_EVENT_QUEUE_OVERFLOW);
			if (err) {
				dev_err(priv->dev, "%s, err:%d\n", __func__, err);
				goto out_fail;
			}
		}

		/* check status for rx message queue trigger position */
		if (intf_pending & SDC_CANFD_REG_INT_STATUS_RX_MSG_QUEUE_ABOVE_TRIGGER_POS) {
#if	DEV_INFO_SHOW
			dev_info(priv->dev, "%s, got SDC_CANFD_REG_INT_STATUS_RX_MSG_QUEUE_ABOVE_TRIGGER_POS\n",
					__func__);
#endif
			err = sdc_canfd_handle_rxif(priv);
			if (err) {
				dev_err(priv->dev, "%s, err:%d\n", __func__, err);
				goto out_fail;
			}
		}

#if	RX_MSG_TIMEOUT_ENABLE
		if (intf_pending & SDC_CANFD_REG_INT_STATUS_RX_MSG_QUEUE_TIMEOUT) {
#if	DEV_INFO_SHOW
			dev_info(priv->dev, "%s, got SDC_CANFD_REG_INT_STATUS_RX_MSG_QUEUE_TIMEOUT\n",
					__func__);
#endif
			err = sdc_canfd_handle_rxif(priv);
			if (err) {
				dev_err(priv->dev, "%s, err:%d\n", __func__, err);
				goto out_fail;
			}
		}
#endif

		/* check status for rx message queue overflow */
		if (intf_pending & SDC_CANFD_REG_INT_STATUS_RX_MSG_QUEUE_OVERFLOW) {

			err = sdc_canfd_handle_ovif(priv, SDC_CANFD_INT_RX_MSG_QUEUE_OVERFLOW);
			if (err) {
				dev_err(priv->dev, "%s, err:%d\n", __func__, err);
				goto out_fail;
			}
		}

		/* check status for error detected */
		if (intf_pending & SDC_CANFD_REG_INT_STATUS_MSG_ERROR_DETECTED) {
#if	DEV_INFO_SHOW
			dev_info_ratelimited(priv->dev, "%s, got SDC_CANFD_REG_INT_STATUS_MSG_ERROR_DETECTED\n",
					__func__);
#endif
			err = sdc_canfd_handle_error_detect(priv);
			if (err) {
				dev_err(priv->dev, "%s, err:%d\n", __func__, err);
				goto out_fail;
			}
		}

		/* check status for bus change state event */
		if (intf_pending & SDC_CANFD_REG_INT_STATUS_BUS_STATE_CHANGE_EVENT) {
			dev_info_ratelimited(priv->dev, "%s, got SDC_CANFD_REG_INT_STATUS_BUS_STATE_CHANGE_EVENT\n",
					__func__);
			err = sdc_canfd_handle_bus_state_change(priv);
			if (err) {
				dev_err(priv->dev, "%s, err:%d\n", __func__, err);
				goto out_fail;
			}
		}

		interrupt_status = readl(priv->event);
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "%s, Read event hdr again, interrupt_status:0x%08x\n",
				__func__, interrupt_status);
#endif
		handled = IRQ_HANDLED;

		/* check if it's status get error detected, mask it */
		if (interrupt_status & SDC_CANFD_REG_INT_STATUS_MSG_ERROR_DETECTED) {

			interrupt_status = interrupt_status & ~SDC_CANFD_REG_INT_STATUS_MSG_ERROR_DETECTED;
			/*dev_info_ratelimited(priv->dev, "%s, Got msg_error, mask it, int_status:0x%08x\n",
			 *	__func__, interrupt_status);
			 */
		}
		/* check if it's status get rx msg queue overflow, mask it */
		if (interrupt_status & SDC_CANFD_REG_INT_STATUS_RX_MSG_QUEUE_OVERFLOW)
			interrupt_status = interrupt_status & ~SDC_CANFD_REG_INT_STATUS_RX_MSG_QUEUE_OVERFLOW;

		/* check if it's status get tx msg queue overflow, mask it */
		if (interrupt_status & SDC_CANFD_REG_INT_STATUS_TX_MSG_QUEUE_OVERFLOW) {

			interrupt_status = interrupt_status & ~SDC_CANFD_REG_INT_STATUS_TX_MSG_QUEUE_OVERFLOW;
			dev_info_ratelimited(priv->dev, "%s, Got tx msg queue overflow, mask it, int_status:0x%08x\n",
				__func__, interrupt_status);
		}
		/* check if it's status get tef overflow, mask it */
		if (interrupt_status & SDC_CANFD_REG_INT_STATUS_TX_EVENT_QUEUE_OVERFLOW) {

			interrupt_status = interrupt_status & ~SDC_CANFD_REG_INT_STATUS_TX_EVENT_QUEUE_OVERFLOW;
			dev_info_ratelimited(priv->dev, "%s, Got tef overflow, mask it, int_status:0x%08x\n",
				__func__, interrupt_status);
		}

#if DEBUG_RECORD_ENABLE
		do_count++;
		if (do_count >= 10) {
			dev_info_ratelimited(priv->dev, "%s, do_count >=10, is:%d, event_hdr:0x%08x\n",
				__func__, do_count, interrupt_status);

		}
#endif

	} while (1);

out_fail:
#if KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE
	can_rx_offload_threaded_irq_finish(&priv->offload);
#endif

	netdev_err(priv->ndev, "IRQ handler returned %d\n", err);

	sdc_canfd_chip_interrupts_disable(priv);

	dev_err(priv->dev, "%s done, out_fail, handled:%d\n", __func__, handled);

	return handled;
}

/* CAN - top half - irq routine, handle interrupt */
static irqreturn_t sdc_canfd_irq_handler(int irq, void *dev_id)
{
	struct sdc_canfd_priv *priv = dev_id;
	unsigned int event;
#if	EVENT_LOCK_ENABLE
	unsigned long flags;
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s(), irq:%d, name:%s\n", __func__,
			irq, priv->ndev->name);
#endif

	event = readl(priv->event);

	/* ignore 0, tx queue empty, tef non empty and rx queue non emmpty */
	if (event == 0 || event == 1 || event == 16 || event == 256) {
#if	DEV_INFO_SHOW
		dev_info_ratelimited(priv->dev, "%s done, event=0,1,16 or 256 is:x%08x, return IRQ_NONE\n",
				__func__, event);
#endif
		return IRQ_NONE;
	}

#if	EVENT_LOCK_ENABLE
	spin_lock_irqsave(&priv->event_lock, flags);
	priv->event_value = event;
	spin_unlock_irqrestore(&priv->event_lock, flags);
#else
	priv->event_value = event;
#endif

	return IRQ_WAKE_THREAD;
}

/* CAN - change socket buffer data to hareware tx object data */
static void sdc_canfd_hw_tx_obj_from_skb(const struct sdc_canfd_priv *priv,
			  struct sdc_canfd_hw_tx_obj_raw *tx_obj,
			  const struct sk_buff *skb,
			  unsigned int seq)
{
	const struct canfd_frame *cfd = (struct canfd_frame *)skb->data;
	u8 dlc;
	u32 id, flags;
	int offset, len;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s(), seq:%d\n", __func__, seq);
#endif
	/* check ext id */
	if (cfd->can_id & CAN_EFF_FLAG) {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "CAN_EFF_FLAG:YES\n");
#endif
		id = cfd->can_id & CAN_EFF_MASK;
		flags = SDC_CANFD_REG_TX_FRAME_EXT_ID_ENABLE;
	} else {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "CAN_EFF_FLAG:NO\n");
#endif
		id = cfd->can_id & CAN_SFF_MASK;
		flags = 0;
	}
#if	DEV_INFO_SHOW
	dev_info(priv->dev, "TX-Encode-1,flags:x%08x, id:x%08x\n", flags, id);
#endif

	/* get dlc */
#if KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE
	dlc = can_fd_len2dlc(cfd->len);
#else
	dlc = can_len2dlc(cfd->len);
#endif

	flags |= FIELD_PREP(SDC_CANFD_REG_TX_FRAME_SEQ_MASK, seq) |
		FIELD_PREP(SDC_CANFD_REG_TX_FRAME_DLC_MASK, dlc);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "TX-Encode-2,flags:x%08x, dlc:x%02x\n", flags, dlc);
#endif

	/* check RTR */
	if (cfd->can_id & CAN_RTR_FLAG) {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "CAN_RTR_FLAG:YES\n");
#endif
		flags |= SDC_CANFD_REG_TX_FRAME_RTR;
	} else {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "CAN_RTR_FLAG:NO\n");
#endif
	}
#if	DEV_INFO_SHOW
	dev_info(priv->dev, "TX-Encode-3,flags:x%08x\n", flags);
#endif

	/* CANFD, FDF and BRS */
	if (can_is_canfd_skb(skb)) {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "canfd_skb:YES\n");
#endif
		flags |= SDC_CANFD_REG_TX_FRAME_FDF;

		if (cfd->flags & CANFD_ESI) {
#if	DEV_INFO_SHOW
			dev_info(priv->dev, "CANFD_ESI:YES\n");
#endif
			flags |= SDC_CANFD_REG_TX_FRAME_ESI;
		}

		if (cfd->flags & CANFD_BRS) {
#if	DEV_INFO_SHOW
			dev_info(priv->dev, "CANFD_BRS:YES\n");
#endif
			flags |= SDC_CANFD_REG_TX_FRAME_BRS;
		}
	} else {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "canfd_skb:NO\n");
#endif
	}

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "TX-Encode-4,flags:x%08x\n", flags);
#endif

	put_unaligned_le32(id, &tx_obj->id);
	put_unaligned_le32(flags, &tx_obj->flags);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "tx_obj->flags:x%08x, tx_obj->id:x%08x\n",
			tx_obj->flags, tx_obj->id);
	dev_info(priv->dev, "flags:x%08x, id:x%08x\n", flags, id);
#endif

	/* Clear data at end of CAN frame */
	offset = round_down(cfd->len, sizeof(u32));

#if KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE
	len = round_up(can_fd_dlc2len(dlc), sizeof(u32)) - offset;
#else
	len = round_up(can_dlc2len(dlc), sizeof(u32)) - offset;
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "offset:%d, len:%d, cfd->len:%d\n",
			offset, len, cfd->len);
#endif

	memset(tx_obj->data + offset, 0x0, len);
	memcpy(tx_obj->data, cfd->data, cfd->len);
#if	DEV_INFO_SHOW
	{
		int i;

		dev_info(priv->dev, "--------------------------------------------\n");
		for (i = 0; i < cfd->len; i++)
			dev_info(priv->dev, "tx_obj->data[%d]:x%02x\n", i, tx_obj->data[i]);
		dev_info(priv->dev, "--------------------------------------------\n");
	}
#endif
}

/* CAN - write tx object data */
static void sdc_canfd_hw_tx_obj_write(const struct sdc_canfd_priv *priv,
				  struct sdc_canfd_hw_tx_obj_raw *tx_obj)
{
	unsigned int i;
	int j = 0;
	u8 len = 0, dlc = 0;

#if	DEV_INFO_SHOW
	u32 val = 0;

	dev_info(priv->dev, "%s()\n", __func__);
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_TX_FRAME_CON0:value = 0x%8x",
			tx_obj->flags);
#endif

	priv->ops->write_reg(priv, SDC_CANFD_REG_TX_FRAME_CON0, tx_obj->flags);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_TX_FRAME_CON0);
	dev_info(priv->dev, "check, SDC_CANFD_REG_TX_FRAME_CON0:value = 0x%8x", val);
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_TX_FRAME_CON1:value = 0x%8x", tx_obj->id);
#endif

	priv->ops->write_reg(priv, SDC_CANFD_REG_TX_FRAME_CON1, tx_obj->id);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_TX_FRAME_CON1);
	dev_info(priv->dev, "check, SDC_CANFD_REG_TX_FRAME_CON1:value = 0x%8x", val);
#endif

	/* Write Data payload */
	if (!(tx_obj->flags & SDC_CANFD_REG_TX_FRAME_RTR)) {

		dlc = FIELD_GET(SDC_CANFD_REG_TX_FRAME_DLC_MASK, tx_obj->flags);

#if KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE
		len = can_fd_dlc2len(dlc);
#else
		len = can_dlc2len(get_canfd_dlc(dlc));
#endif

#if	DEV_INFO_SHOW
		dev_info(priv->dev, "len:%d", len);
#endif
		for (i = 0; i < len; i += 4) {
			u32 data = le32_to_cpu(*(__le32 *)(tx_obj->data + i));

#if	DEV_INFO_SHOW
			dev_info(priv->dev, "#%d. SDC_CANFD_REG_TX_MSG_DW%d(0x%2x), value = 0x%8x",
					i, j, SDC_CANFD_REG_TX_MSG_DW0+j, data);
#endif
			priv->ops->write_reg(priv, SDC_CANFD_REG_TX_MSG_DW0+j, data);
			j++;
		}
	}

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_TX_MSG_CON:value = 0x%8x",
			(unsigned int)SDC_CANFD_REG_TX_MSG_INCREASE_MSG_COUNT);
#endif
	/* increase tx message count */
	priv->ops->write_reg(priv, SDC_CANFD_REG_TX_MSG_CON,
				SDC_CANFD_REG_TX_MSG_INCREASE_MSG_COUNT);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

}

/* check tx message queue is full or not, also check tx_head and tx_tail */
static bool sdc_canfd_hw_tx_busy(const struct sdc_canfd_priv *priv)
{
	u32 val = 0;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

	val = priv->ops->read_reg(priv, SDC_CANFD_REG_INT_STATUS);
#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_INT_STATUS, val:0x%8x", val);
#endif

	if (val & SDC_CANFD_REG_INT_STATUS_TX_MSG_QUEUE_FULL) {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "Got SDC_CANFD_REG_INT_STATUS_TX_MSG_QUEUE_FULL");
#endif

#if	TX_OVERFLOW_TEST_ENABLE
		return false;
#else
		return true;
#endif
	}

	return false;
}

/* CAN - socket tx function */
static netdev_tx_t sdc_can_start_xmit(struct sk_buff *skb,
					struct net_device *ndev)
{
	struct sdc_canfd_priv *priv = netdev_priv(ndev);
	struct sdc_canfd_hw_tx_obj_raw tx_obj;
	u8 tx_head_pos = 0;
#if DEBUG_RECORD_ENABLE
	u32 val;
#endif

#if	TX_LOCK_ENABLE
	unsigned long flags;
#endif

#if	DEV_INFO_SHOW
	netdev_info(ndev, "%s( )\n", __func__);
#endif

	if (can_dropped_invalid_skb(ndev, skb)) {
		netdev_err(ndev, "%s, can_dropped_invalid_skb:true\n", __func__);
		return NETDEV_TX_OK;
	}
	/* check tx message queue is full or not,
	 * check tx_head and tx_tail
	 */
	if (sdc_canfd_hw_tx_busy(priv)) {
		netif_stop_queue(ndev);
		/*netdev_info(ndev, "%s, sdc_canfd_tx_busy:true\n", __func__); */
		return NETDEV_TX_BUSY;
	}

	sdc_canfd_hw_tx_obj_from_skb(priv, &tx_obj, skb, priv->tx_head);


#if	TX_LOCK_ENABLE
	spin_lock_irqsave(&priv->tx_lock, flags);
#if	DEV_INFO_SHOW
	dev_info(priv->dev, "HWTX, SUNIX_TX_OBJ_NUM_MAX(%d) - (priv->tx_head(%u) - priv->tx_tail(%u)) = %u\n",
			SUNIX_TX_OBJ_NUM_MAX, priv->tx_head, priv->tx_tail,
			SUNIX_TX_OBJ_NUM_MAX - (priv->tx_head - priv->tx_tail));
#endif
#endif

	tx_head_pos = priv->tx_head & (SUNIX_TX_OBJ_NUM_MAX - 1);
#if	DEV_INFO_SHOW
	netdev_info(ndev, "tx_head:x%02x, tx_head_pos:x%02x\n",
			priv->tx_head, tx_head_pos);
#endif

	priv->tx_head++;
	if (priv->tx_head - priv->tx_tail >= SUNIX_TX_OBJ_NUM_MAX) {
		netif_stop_queue(ndev);
#if	DEV_INFO_SHOW
		netdev_info(ndev, "tx_head(%u) - tx_tail(%d) >= %u\n",
				priv->tx_head, priv->tx_tail, SUNIX_TX_OBJ_NUM_MAX);
#endif
	}

#if	DEV_INFO_SHOW
	netdev_info(ndev, "increase tx_head:%u\n", priv->tx_head);
#endif

#if	TX_LOCK_ENABLE
	spin_unlock_irqrestore(&priv->tx_lock, flags);
#endif

#if KERNEL_VERSION(5, 12, 0) <= LINUX_VERSION_CODE
	can_put_echo_skb(skb, ndev, tx_head_pos, 0);
#else
	can_put_echo_skb(skb, ndev, tx_head_pos);
#endif

	sdc_canfd_hw_tx_obj_write(priv, &tx_obj);

#if	TEF_OVERFLOW_TEST_ENABLE
	priv->tx_tail++;
	{
		struct net_device_stats *stats = &priv->ndev->stats;
		u8 tx_tail_pos, timestamp = 0;

		tx_tail_pos = priv->tx_tail & (SUNIX_TX_OBJ_NUM_MAX - 1);
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "tx_tail = %u, tx_tail_pos = 0x%8x",
				priv->tx_tail, tx_tail_pos);
#endif
		stats->tx_bytes +=
		/* can rx offload queue work */
#if KERNEL_VERSION(5, 12, 0) <= LINUX_VERSION_CODE
		can_rx_offload_get_echo_skb(&priv->offload,
					    tx_tail_pos,
					    timestamp, NULL);
#elif KERNEL_VERSION(4, 20, 0) <= LINUX_VERSION_CODE
		can_rx_offload_get_echo_skb(&priv->offload,
					    tx_tail_pos,
					    timestamp);
#else
		can_get_echo_skb(priv->ndev, tx_tail_pos);
#endif
		stats->tx_packets++;

		if (SUNIX_TX_OBJ_NUM_MAX - (priv->tx_head - priv->tx_tail) > 0)
			netif_wake_queue(priv->ndev);
	}
#endif

#if DEBUG_RECORD_ENABLE
	priv->tx_count++;
	val = priv->tx_count / 1000000;
	if (val != priv->tx_div) {
		priv->tx_div = val;
		dev_info(priv->dev, "tx_count:%lu\n", priv->tx_count);
	}
#endif

#if	DEV_INFO_SHOW
	netdev_info(ndev, "%s done\n", __func__);
#endif

	return NETDEV_TX_OK;
}

/* CAN - socket open function */
static int sdc_can_open(struct net_device *ndev)
{
	int err;
	struct sdc_canfd_priv *priv = netdev_priv(ndev);

#if	DEV_INFO_SHOW
	netdev_info(ndev, "%s()\n", __func__);
#endif

#if	DEV_RUNTIME_PM_FUNC_ENABLE
	err = pm_runtime_get_sync(ndev->dev.parent);
	if (err < 0) {
		pm_runtime_put_noidle(ndev->dev.parent);
		netdev_err(ndev, "%s err, err:%d\n", __func__, err);
		return err;
	}
#endif

	err = open_candev(ndev);
	if (err) {
		netdev_err(ndev, "%s err , err:%d\n", __func__, err);
#if	DEV_RUNTIME_PM_FUNC_ENABLE
		goto out_pm_runtime_put;
#endif
	}

	err = sdc_canfd_chip_start(priv);
	if (err) {
		netdev_err(ndev, "%s err, err:%d\n", __func__, err);
		goto out_close_candev;
	}

	can_rx_offload_enable(&priv->offload);

	err = request_threaded_irq(ndev->irq, sdc_canfd_irq_handler,
					sdc_canfd_thread_fn,
					IRQF_SHARED, dev_name(&ndev->dev),
					priv);
	if (err) {
		netdev_err(ndev, "%s err, err:%d\n", __func__, err);
		goto out_can_rx_offload_disable;
	}

	err = sdc_canfd_chip_interrupts_enable(priv);
	if (err) {
		netdev_err(ndev, "%s err, err:%d\n", __func__, err);
		goto out_free_irq;
	}

	netif_start_queue(ndev);

#if	DEV_INFO_SHOW
	netdev_info(ndev, "%s done\n", __func__);
#endif

	return 0;

out_free_irq:
	free_irq(ndev->irq, priv);

out_can_rx_offload_disable:
	can_rx_offload_disable(&priv->offload);

out_close_candev:
	close_candev(ndev);

	sdc_canfd_chip_stop(priv, CAN_STATE_STOPPED);

#if	DEV_RUNTIME_PM_FUNC_ENABLE
out_pm_runtime_put:
	pm_runtime_put(ndev->dev.parent);
#endif

#if	DEV_INFO_SHOW
	netdev_err(ndev, "%s done with err:%d\n", __func__, err);
#endif

	return err;
}

/* CAN - socket close function */
static int sdc_can_stop(struct net_device *ndev)
{
	struct sdc_canfd_priv *priv = netdev_priv(ndev);

#if	DEV_INFO_SHOW
	netdev_info(ndev, "%s()\n", __func__);
#endif

	netif_stop_queue(ndev);
	sdc_canfd_chip_interrupts_disable(priv);
	free_irq(ndev->irq, priv);
	can_rx_offload_disable(&priv->offload);
	sdc_canfd_chip_stop(priv, CAN_STATE_STOPPED);
	close_candev(ndev);

#if	DEV_RUNTIME_PM_FUNC_ENABLE
	pm_runtime_put(ndev->dev.parent);
#endif

#if	DEV_INFO_SHOW
	netdev_info(ndev, "%s() done\n", __func__);
#endif

	return 0;
}

static const struct net_device_ops sdc_netdev_ops = {
	.ndo_open = sdc_can_open,
	.ndo_stop = sdc_can_stop,
	.ndo_start_xmit	= sdc_can_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

#if KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE
/* NAPI */
static int __can_rx_offload_napi_poll(struct napi_struct *napi, int quota)
{
	struct can_rx_offload *offload = container_of(napi, struct can_rx_offload, napi);
	struct net_device *dev = offload->dev;
	struct net_device_stats *stats = &dev->stats;
	struct sk_buff *skb;
	int work_done = 0;

	while ((work_done < quota) &&
	       (skb = skb_dequeue(&offload->skb_queue))) {
		struct can_frame *cf = (struct can_frame *)skb->data;

		work_done++;
		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_receive_skb(skb);
	}

	if (work_done < quota) {
		napi_complete_done(napi, work_done);

		// Check if there was another interrupt
		if (!skb_queue_empty(&offload->skb_queue))
			napi_reschedule(&offload->napi);
	}

	can_led_event(offload->dev, CAN_LED_EVENT_RX);

	return work_done;
}

static int __can_rx_offload_init_queue(struct net_device *dev,
								struct can_rx_offload *offload,
								unsigned int weight)
{
#if	DEV_INFO_SHOW
	dev_info(dev->dev.parent, "%s( )\n", __func__);
#endif

	offload->dev = dev;

	// Limit queue len to 4x the weight (rounted to next power of two) //
	offload->skb_queue_len_max = 2 << fls(weight);
	offload->skb_queue_len_max *= 4;
	skb_queue_head_init(&offload->skb_queue);

	/* can rx offload queue work */
#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
	can_rx_offload_reset(offload);
#endif

	netif_napi_add(dev, &offload->napi, __can_rx_offload_napi_poll, weight);

	dev_dbg(dev->dev.parent, "%s: skb_queue_len_max=%d\n",
		__func__, offload->skb_queue_len_max);

#if	DEV_INFO_SHOW
	dev_info(dev->dev.parent, "%s( ) done\n", __func__);
#endif

	return 0;
}
#endif

/* CAN - set operation mode */
static int sdc_canfd_set_mode(struct net_device *ndev, enum can_mode mode)
{
	struct sdc_canfd_priv *priv = netdev_priv(ndev);
	int err;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s()\n", __func__);
#endif

	switch (mode) {
	case CAN_MODE_START:
		err = sdc_canfd_chip_start(priv);
		if (err) {
			dev_err(priv->dev, "%s, err:%d\n", __func__, err);
			return err;
		}

		err = sdc_canfd_chip_interrupts_enable(priv);
		if (err) {
			sdc_canfd_chip_stop(priv, CAN_STATE_STOPPED);
			dev_err(priv->dev, "%s, err:%d\n", __func__, err);
			return err;
		}

		netif_wake_queue(ndev);
		break;

	default:
		dev_err(priv->dev, "%s err, uncheck mode:%d\n", __func__, mode);
		return -EOPNOTSUPP;
	}

	return 0;
}

// Added by Andy
#if	TERMINATION_SET_ENABLE
/* CAN - set termination callback */
static int sdc_canfd_set_termination(struct net_device *ndev, u16 term_req)
{
	struct sdc_canfd_priv *priv = netdev_priv(ndev);
	u32 val;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s(), term_req:x%02x, can.termination:x%02x\n",
		__func__, term_req, priv->can.termination);
#endif

	val = priv->ops->read_reg(priv, SDC_CANFD_REG_CON);
#if	DEV_INFO_SHOW
	dev_info(priv->dev, "SDC_CANFD_REG_CON:value = 0x%8x", val);
#endif

	if (term_req)
		val = (val | SDC_CANFD_REG_CON_TERMINATION);
	else
		val = (val & ~SDC_CANFD_REG_CON_TERMINATION);

	priv->ops->write_reg(priv, SDC_CANFD_REG_CON, val);

#if	DEV_CHECK_REG_ENABLE
	val = priv->ops->read_reg(priv, SDC_CANFD_REG_CON);
	dev_info(priv->dev, "check, SDC_CANFD_REG_CON:value = 0x%8x", val);
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

	return 0;
}
#endif

/* CAN - register CAN */
static int sdc_can_register(struct sdc_canfd_priv *priv)
{
	struct net_device *ndev = priv->ndev;
	int err;

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s\n", __func__);
#endif

#if	DEV_RUNTIME_PM_FUNC_ENABLE
	pm_runtime_get_noresume(ndev->dev.parent);
	err = pm_runtime_set_active(ndev->dev.parent);
	if (err) {
		dev_err(priv->dev, "%s, err:%d\n", __func__, err);
		goto out_runtime_put_noidle;
	}
	pm_runtime_enable(ndev->dev.parent);
#endif

	err = sdc_canfd_chip_softreset(priv);
	if (err == -ENODEV) {
		dev_err(priv->dev, "%s, err:%d\n", __func__, err);
#if	DEV_RUNTIME_PM_FUNC_ENABLE
		goto out_runtime_disable;
#endif
		return err;
	}

	err = register_candev(ndev);
	if (err) {
		dev_err(priv->dev, "%s err 7, err:%d\n", __func__, err);
		goto out_chip_set_mode_config;
	}

#if	DEV_RUNTIME_PM_FUNC_ENABLE
	pm_runtime_put(ndev->dev.parent);
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s done\n", __func__);
#endif

	return 0;

out_chip_set_mode_config:
	sdc_canfd_chip_set_config_mode(priv, SDC_CANFD_REG_CON_MODE_CONFIG);

#if	DEV_RUNTIME_PM_FUNC_ENABLE
out_runtime_disable:
	pm_runtime_disable(ndev->dev.parent);
out_runtime_put_noidle:
	pm_runtime_put_noidle(ndev->dev.parent);
#endif

	dev_err(priv->dev, "%s done with err:%d\n", __func__, err);
	return err;
}

/* CAN - proc file content */
#ifdef CONFIG_PROC_FS
#if	PROC_FS_ENABLE
static int sdc_can_proc_show(struct seq_file *m, void *v)
{
	struct sdc_canfd_priv *priv = m->private;

	seq_printf(m, "pci_bus=%d;irq=%d;index=%d;pid=%d;",
		priv->pdata->board.pci_bus, priv->pdata->board.irq, priv->pdata->index,
		priv->pdev->id);
	seq_putc(m, '\n');

	return 0;
}

#if KERNEL_VERSION(4, 18, 0) > LINUX_VERSION_CODE
static int sdc_can_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, sdc_can_proc_show, PDE_DATA(inode));
}

static const struct file_operations sdc_can_proc_fops = {
	.open		= sdc_can_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

/* CAN - create proc file */
static int sdc_can_proc_create(struct sdc_canfd_priv *priv)
{
	if (!driver_ent)
		return -ENOMEM;

#if KERNEL_VERSION(4, 18, 0) <= LINUX_VERSION_CODE
	priv->dev_ent =	proc_create_single_data(priv->ndev->name, 0444, driver_ent,
											sdc_can_proc_show, priv);
#else
	priv->dev_ent = proc_create_data(priv->ndev->name, 0444, driver_ent,
											&sdc_can_proc_fops, priv);
#endif
	if (!priv->dev_ent)
		return -ENOMEM;

	return 0;
}

/* CAN - remove proc file */
static void sdc_can_proc_remove(struct sdc_canfd_priv *priv)
{
	if (priv->dev_ent)
		remove_proc_entry(priv->ndev->name, driver_ent);
}
#endif
#endif

/* platform - CAN probe function */
static int sdc_can_probe(struct platform_device *pdev)
{
	struct net_device *ndev;
	struct sdc_can_platdata *pdata;
	struct sdc_canfd_priv *priv;
	int err, irq, addr_size;
	void __iomem *addr, *event;
	struct resource *res;
	u32 val;

#if	DEV_INFO_SHOW
	dev_info(&pdev->dev, "%s( )\n", __func__);
#endif

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata)
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	addr_size = resource_size(res);
	if (addr_size < 0)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);

	event = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->event))
		return PTR_ERR(event);

	/* Create a CAN device instance */
	ndev = alloc_candev(sizeof(struct sdc_canfd_priv),
			    SUNIX_TX_OBJ_NUM_MAX);
	if (!ndev)
		return -ENOMEM;

	ndev->netdev_ops = &sdc_netdev_ops;
	ndev->irq = irq;
	ndev->flags |= IFF_ECHO;

#if	EVENT_LOCK_ENABLE
	spin_lock_init(&priv->event_lock);
#endif

#if	TX_LOCK_ENABLE
	spin_lock_init(&priv->tx_lock);
#endif

	priv = netdev_priv(ndev);

	priv->can.clock.freq = SDC_CANFD_REF_CLK;
	priv->can.do_set_mode = sdc_canfd_set_mode;
	priv->can.do_get_berr_counter = sdc_canfd_get_berr_counter;
	priv->can.bittiming_const = &sdc_canfd_bittiming_const;
#if KERNEL_VERSION(6, 16, 0) <= LINUX_VERSION_CODE
	priv->can.fd.data_bittiming_const = &sdc_canfd_data_bittiming_const;
#else
	priv->can.data_bittiming_const = &sdc_canfd_data_bittiming_const;
#endif

	priv->can.ctrlmode_supported = CAN_CTRLMODE_LISTENONLY |
		CAN_CTRLMODE_BERR_REPORTING | CAN_CTRLMODE_FD |
		CAN_CTRLMODE_FD_NON_ISO | CAN_CTRLMODE_ONE_SHOT |
		CAN_CTRLMODE_3_SAMPLES | CAN_CTRLMODE_LOOPBACK;

	priv->base = addr;
	priv->base_size = addr_size;
#if	DEV_INFO_SHOW
	dev_info(&pdev->dev, "priv, base:%p, base_size = 0x%4x\n",
			priv->base, priv->base_size);
#endif
	priv->event = event;
#if	DEV_INFO_SHOW
	dev_info(&pdev->dev, "priv, event:%p\n", priv->event);
#endif
	priv->ops = &st_sdc_canfd_ops;
	priv->ndev = ndev;

	// Added by Andy
	priv->pdata	= pdata;
	if (priv->pdata->capability & 0x01) {
#if	TERMINATION_SET_ENABLE
		priv->can.termination_const = termination_const;
		priv->can.termination_const_cnt = ARRAY_SIZE(termination_const);
		// Change by Jason Lee, 20240627,
		priv->can.termination = SDC_CANFD_TERMINATION_EANBLED;  /* default enable */
		priv->can.do_set_termination = sdc_canfd_set_termination;
		// Add by Jason Lee, 20240627, default enable
		val = priv->ops->read_reg(priv, SDC_CANFD_REG_CON);
		val = (val | SDC_CANFD_REG_CON_TERMINATION);
		priv->ops->write_reg(priv, SDC_CANFD_REG_CON, val);
#endif
	}

	/* pass platform device */
	priv->pdev = pdev;
	priv->dev = &pdev->dev;

	platform_set_drvdata(pdev, priv);
	SET_NETDEV_DEV(ndev, &pdev->dev);

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	err = can_rx_offload_add_manual(ndev, &priv->offload,
					SUNIX_NAPI_WEIGHT);
#else
	err = __can_rx_offload_init_queue(ndev, &priv->offload,
					SUNIX_NAPI_WEIGHT);
#endif
	if (err) {
		dev_err(&pdev->dev, "%s, err:%d\n", __func__, err);
		goto out_free_candev;
	}

	err = sdc_can_register(priv);
	if (err) {
		dev_err(&pdev->dev, "%s, err:%d\n", __func__, err);
		goto out_can_rx_offload_del;
	}

	// Change by Jason Lee, 20240627,
	netdev_info(priv->ndev,
		    "SUNCAN (Capability:x%08X, CAN_CLK:%u.%02uMHz SYS_CLK:%u.%02uMHz) successfully initialized.\n",
		    priv->pdata->capability,
		    priv->can.clock.freq / 1000000,
		    priv->can.clock.freq % 1000000 / 1000 / 10,
		    SDC_CANFD_SYS_CLK / 1000000,
		    SDC_CANFD_SYS_CLK % 1000000 / 1000 / 10);

#ifdef CONFIG_PROC_FS
#if PROC_FS_ENABLE
	err = sdc_can_proc_create(priv);
	if (err)
		dev_warn(&pdev->dev, "%s, failed to create device proc entry\n",
			__func__);
#endif
#endif

#if	DEV_INFO_SHOW
	dev_info(&pdev->dev, "%s( ) done\n", __func__);
#endif

	return 0;

out_can_rx_offload_del:
	can_rx_offload_del(&priv->offload);
out_free_candev:
	free_candev(ndev);
	dev_err(&pdev->dev, "%s done with err:%d\n", __func__, err);
	return err;
}

/* platform - CAN remove function */
#if KERNEL_VERSION(6, 11, 0) <= LINUX_VERSION_CODE
static void sdc_can_remove(struct platform_device *pdev)
#else
static int sdc_can_remove(struct platform_device *pdev)
#endif
{
	struct sdc_canfd_priv *priv = platform_get_drvdata(pdev);
	struct net_device *ndev = priv->ndev;

#if	DEV_INFO_SHOW
	dev_info(&pdev->dev, "%s( )\n", __func__);
#endif

#ifdef CONFIG_PROC_FS
#if PROC_FS_ENABLE
	sdc_can_proc_remove(priv);
#endif
#endif

	/* other driver do this on device close */
	can_rx_offload_del(&priv->offload);
	unregister_candev(ndev);
#if	DEV_RUNTIME_PM_FUNC_ENABLE
	pm_runtime_get_sync(ndev->dev.parent);
	pm_runtime_put_noidle(ndev->dev.parent);
	pm_runtime_disable(ndev->dev.parent);
#endif
	free_candev(ndev);

#if	DEV_INFO_SHOW
	dev_info(&pdev->dev, "%s( ) done\n", __func__);
#endif

#if KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE
	return 0;
#endif
}

/* platform - power ops - resume function */
static int sdc_platform_resume(struct device *pdev)
{
	struct sdc_canfd_priv *priv = dev_get_drvdata(pdev);
#if	DEV_SLEEP_PM_OPS_WORK_ENABLE
	int ret;
#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s( )\n", __func__);
#endif

#if	DEV_SLEEP_PM_OPS_WORK_ENABLE
	/* refer from ctucanfd */
	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	if (netif_running(priv->ndev)) {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "%s( ), netif is running, start queue", __func__);
#endif
		ret = sdc_canfd_chip_start(priv);
		if (ret) {
			dev_err(priv->dev, "sdc_canfd_chip_start failed on resume\n");
			return ret;
		}
		netif_device_attach(priv->ndev);
		netif_start_queue(priv->ndev);
	}

#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s( ) done\n", __func__);
#endif

	return 0;
}

/* platform - power ops - suspend function */
static int sdc_platform_suspend(struct device *pdev)
{
	struct sdc_canfd_priv *priv = dev_get_drvdata(pdev);

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s( )\n", __func__);
#endif

#if	DEV_SLEEP_PM_OPS_WORK_ENABLE
	/* refer from ctucanfd */
	if (netif_running(priv->ndev)) {
#if	DEV_INFO_SHOW
		dev_info(priv->dev, "%s( ), netif is running, stop queue", __func__);
#endif
		netif_stop_queue(priv->ndev);
		netif_device_detach(priv->ndev);

		sdc_canfd_chip_stop(priv, CAN_STATE_STOPPED);
	}

	priv->can.state = CAN_STATE_SLEEPING;

#endif

#if	DEV_INFO_SHOW
	dev_info(priv->dev, "%s( ) done\n", __func__);
#endif

	return 0;
}

static const struct dev_pm_ops sdc_platform_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdc_platform_suspend, sdc_platform_resume)
};

static struct platform_driver sdc_platform_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &sdc_platform_pm_ops,
	},
	.probe = sdc_can_probe,
	.remove = sdc_can_remove,
};

static int __init sdc_can_init(void)
{
	int ret;

#if	CAN_DEBUG_INIT_AND_EXIT
	pr_info("%s( )\n", __func__);
#endif

#ifdef CONFIG_PROC_FS
#if PROC_FS_ENABLE
	driver_ent = proc_mkdir(DRV_NAME, NULL);
	if (!driver_ent)
		pr_warn("%s(), failed to create driver proc entry\n", __func__);
#endif
#endif

	ret = platform_driver_register(&sdc_platform_driver);
	if (ret < 0)
		goto out_remove_proc_entry;

	return 0;

out_remove_proc_entry:
#ifdef CONFIG_PROC_FS
#if PROC_FS_ENABLE
	if (driver_ent)
		remove_proc_entry(DRV_NAME, NULL);
#endif
#endif

	return ret;
}
module_init(sdc_can_init);

static void __exit sdc_can_exit(void)
{
#if	CAN_DEBUG_INIT_AND_EXIT
	pr_info("%s( )\n", __func__);
#endif

	platform_driver_unregister(&sdc_platform_driver);

#ifdef CONFIG_PROC_FS
#if PROC_FS_ENABLE
	if (driver_ent)
		remove_proc_entry(DRV_NAME, NULL);
#endif
#endif
}
module_exit(sdc_can_exit);

MODULE_AUTHOR("Andy Jheng <andy_jheng@sunix.com>");
MODULE_DESCRIPTION("SUNIX SDC CAN FD controller driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);

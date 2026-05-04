/**
 * @file global.h
 * @brief all parameter definition
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 * 
 * @author Andy Jheng (andy_jheng@sunix.com)
 * 
 * @copyright Copyright (c) 2022  SUNIX Co., Ltd.
 * 
 */

#ifndef _SOCKETCAN_PROG_SXXG_H_
#define _SOCKETCAN_PROG_SXXG_H_

#define INPUT_PARAMTER_DISABLE				0			/*!< disable user input */
#define INPUT_PARAMTER_ENABLE				1			/*!< enable user input */

#define RANDOM_DATA_DISABLE					0			/*!< disable random data */
#define RANDOM_DATA_ENABLE					1			/*!< enable random data */

#define TYPE_CAN							0x00        /*!< Type is CAN */
#define TYPE_CAN_FD							0x01        /*!< TYPE is CAN FD */

#define MESSAGE_STANDARD					0x00        /*!< Message type is standard (11 bit) */
#define MESSAGE_EXTENDED					0x01        /*!< Message type is extended (29 bit) */
#define MESSAGE_RTR							0x02        /*!< Message type is remote request */
#define MESSAGE_FD_BRS						0x04        /*!< Message type is bitrate switch (FD) */

#define BUFF_LEN							64			/*!< Buffer length */
#define MAXIMUM_IDFILTER_AMOUNT				32          /*!< The maximum limit of id filter */

#define ENABLE_ALL_CAN_ERROR_MASKS			0			/*!< enable all can error mask */

// global using
/*!< can device - Socket descriptor */
extern int sd;
/*!< can device - name */
extern char can_net_name[BUFF_LEN];
/*!< can type - CAN or CAN FD */
extern int can_type;
/*!< can device - rx thread create flag */
extern int rxthd_create_flag;
/*!< can device - rx thread handle */
extern pthread_t rxthd_hd;
// can deivce - receive filter
/*!< can deivce - receive filter */
extern struct can_filter* prfilter;

extern struct sockaddr_can gcan_addr;

#endif

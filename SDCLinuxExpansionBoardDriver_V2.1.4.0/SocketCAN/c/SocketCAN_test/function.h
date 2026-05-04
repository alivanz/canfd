/**
 * @file function.h
 * @brief utils and SocketCAN function definition
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * 
 * @author Andy Jheng (andy_jheng@sunix.com)
 * 
 * @copyright Copyright (c) 2022  SUNIX Co., Ltd.
 * 
 */


#ifndef _SOCKETCAN_PROG_SXXF_H_
#define _SOCKETCAN_PROG_SXXF_H_

/*-------------------------------------------------------------------*/
// basic utils (utils.c)
/**
 * @fn void print_l(int button)
 * @brief print some message
 * 
 * @param[in] button integer, message print information
 * @return void
 * @note   
 */
void print_l(int button);
/**
 * @fn sgetche(void)
 * @brief get user input character
 * 
 * @return char
 * @note   
 */
char sgetche(void);

/*-------------------------------------------------------------------*/
// SocketCAN utils input (utils.c)
/**
 * @fn input_msg_user_input(void)
 * @brief user input enable or disable when write message
 * 
 * @return int
 * @note   0:user input disable, 1: user input enable 
 */
int input_msg_user_input(void);
/**
 * @fn input_msg_id(void)
 * @brief user input messeage id
 * 
 * @return unsigned int
 * @note
 */
unsigned int input_msg_id(void);
/**
 * @fn input_msg_type(void)
 * @brief user input message type
 * 
 * @return unsigned int
 * @note value can combine together(MESSAGE_STANDARD, MESSAGE_EXTENEDED, MESSAGE_RTR, MESSAGE_FD_BRS)
 */
unsigned int input_msg_type(void);
/**
 * @fn input_msg_len_n()
 * @brief user input messeage length
 * 
 * @return unsigned int
 * @note
 */
unsigned int input_msg_len_n();
/**
 * @fn input_msg_data_random_enable(void)
 * @brief generate random data or not
 * 
 * @return int
 * @note   0:no random, 1: random
 */
int input_msg_data_random_enable(void);
/**
 * @fn input_msg_data(void)
 * @brief user input messeage data
 * 
 * @return unsigned char
 * @note
 */
unsigned char input_msg_data(void);
/**
 * @fn input_can_net_device_name(char* name)
 * @brief user input can net device name
 * 
 * @param[in] name char pointer, CAN net device name
 * @return int
 * @note   0:success, -1: fail
 */
int input_can_net_device_name(char* name);
/**
 * @fn input_id_filter_amount(void)
 * @brief user input id filter amount
 * 
 * @return int
 * @note   range from 1 ~ 32
 */
int input_id_filter_amount(void);
/**
 * @fn input_id_filter_frame_type(void)
 * @brief user input id filter frame type
 * 
 * @return int
 * @note   range from 0 ~ 1
 */
int input_id_filter_frame_type(void);

/*-------------------------------------------------------------------*/
// SocketCAN test function (socketcan_funct_test.c)
/**
 * @fn socketcan_open()
 * @brief open socket, thread and do relative actions
 * 
 * @return int
 * @note   0:success, -1: fail
 */
int socketcan_open();
/**
 * @fn socketcan_close()
 * @brief close socket and thread
 * 
 * @return int
 * @note   0:success, -1: fail
 */
int socketcan_close();
/**
 * @fn socketcan_write_data_can(int is_input_para,unsigned int id,unsigned char *data,int data_len,int message_type_flags)
 * @brief write CAN message
 * 
 * @param[in] is_input_para int, 0:user input disable, 1:user input enable
 * @param[in] id unsigned int, message id
 * @param[in] data unsigned char pointer, message data
 * @param[in] data_len int, message data length(range from 0 to 8)
 * @param[in] message_type_flags int, value can combine together(MESSAGE_STANDARD, MESSAGE_EXTENEDED, MESSAGE_RTR)
 * @return int
 * @note   0:success, -1: fail
 */
int socketcan_write_data_can(
    int is_input_para,
    unsigned int id,
    unsigned char *data,
    int data_len,
    int message_type_flags);
/**
 * @fn socketcan_write_data_can_fd(int is_input_para,unsigned int id,unsigned char *data,int data_len,int message_type_flags)
 * @brief write CAN FD message
 * 
 * @param[in] is_input_para int, 0:user input disable, 1:user input enable
 * @param[in] id unsigned int, message id
 * @param[in] data unsigned char pointer, message data
 * @param[in] data_len int, message data length(range from 0 to 64)
 * @param[in] message_type_flags int, value can combine together(MESSAGE_STANDARD, MESSAGE_EXTENEDED, MESSAGE_FD_BRS)
 * @return int
 * @note   0:success, -1: fail
 */
int socketcan_write_data_can_fd(
    int is_input_para,
    unsigned int id,
    unsigned char *data,
    int data_len,
    int message_type_flags);
/**
 * @fn socketcan_set_recv_filter()
 * @brief set recevie filter
 * 
 * @return int
 * @note   0:success, -1: fail
 */    
int socketcan_set_recv_filter();
/**
 * @fn socketcan_clear_recv_filter()
 * @brief clear receive filter
 * 
 * @return int
 * @note   0:success, -1: fail
 */
int socketcan_clear_recv_filter();

#endif

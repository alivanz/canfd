/**
 * @file main.c
 * @brief cmd list for test SocketCAN function
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2022, SUNIX Co., Ltd.
 *
 * Authors      : Andy Jheng <andy_jheng@sunix.com>
 * 
 * 
**/

#include "precomp.h"

#define DATE    "2022/09/12"
#define VERSION "0.0.1.0"

// CAN block
int  sd = -1;
char can_net_name[BUFF_LEN] = {0};
int can_type = TYPE_CAN;
int rxthd_create_flag = 0;
pthread_t rxthd_hd = 0;
struct can_filter* prfilter = NULL;

struct sockaddr_can gcan_addr;

/*-----------------------------------------------------------------------------*/

static void print_info(void)
{
    printf("---------------------------------------\n");
    printf(" Date        : %s\n", DATE);
    printf(" Version     : %s\n", VERSION);
    printf("---------------------------------------\n");
}

/*-----------------------------------------------------------------------------*/

static void print_cmd(void)
{
    printf(">-------------------------------------<\n");
    printf("    A : Open \n");
    printf("    B : Close \n");
    printf("    C : Write CAN data\n");
    printf("    D : Write CAN FD data\n");
    printf("    E : Set receive filter\n");
    printf("    F : Clear receive filter\n");

    printf("\n");
    printf("    ? : Show cmd\n");
    printf("    Q : quit\n");
    printf(">-------------------------------------<\n\n");
}

/*-----------------------------------------------------------------------------*/

int main(void)
{
    int cmd, i = 0, ret_n = 0, user_input_enable = INPUT_PARAMTER_DISABLE;
    unsigned char data[BUFF_LEN] = {0};
   
    system("clear");
    print_info();
    printf("=========================================================================================\n");
    printf("|                                  Socketcan R/W Example                                 |\n");
    printf("=========================================================================================\n");
    printf("Usage before run program                                                                 |\n");
    printf("   Enable CAN device:                                                                    |\n");
    printf("       sudo ip link set can0 type can bitrate 1000000                                    |\n");
    printf("       sudo ip link set can0 up                                                          |\n");
    printf("   or                                                                                    |\n");
    printf("   Enable CAN FD device:                                                                 |\n");
    printf("       sudo ip link set can0 type can bitrate 1000000 dbitrate 2000000 fd on                                     |\n");
    printf("       sudo ip link set can0 up                                                          |\n");
    printf("=========================================================================================\n");
    print_cmd();

    do {
        printf("d[#_#]b > CMD ?\n");
        cmd = sgetche();
        printf("\n");

        switch (cmd) {
            case 'a':
            case 'A':
                ret_n = socketcan_open();
                if (ret_n >= 0) {
                    printf("#INFO: Socketcan net device open Success\n");
                } else {
                    printf("#ERROR: Socketcan net device open Fail\n");
                }
                break;
            case 'b':
            case 'B':
                ret_n = socketcan_close();
                if (ret_n >= 0) {
                    printf("#INFO: Socketcan net device close Success\n");
                } else {
                    printf("#ERROR: Socketcan net device close Fail\n");
                }
                break;           
            case 'c':
            case 'C':
                user_input_enable = input_msg_user_input();
                if (user_input_enable == INPUT_PARAMTER_DISABLE) {
                    for (i = 0; i < CAN_MAX_DLEN; i++) {
                        data[i] = 0x01 + i;
                    }
                    socketcan_write_data_can(
                        INPUT_PARAMTER_DISABLE,
                        0x01, data, CAN_MAX_DLEN, MESSAGE_STANDARD);
                } else {
                    socketcan_write_data_can(
                    INPUT_PARAMTER_ENABLE,
                    0, data, 8, MESSAGE_STANDARD);
                }
                break;
            case 'd':
            case 'D':
                user_input_enable = input_msg_user_input();
                if (user_input_enable == INPUT_PARAMTER_DISABLE) {
                    for (i = 0; i < CANFD_MAX_DLEN; i++) {
                        data[i] = 0x01 + i;
                    }
                    socketcan_write_data_can_fd(
                        INPUT_PARAMTER_DISABLE,
                        0x01, data, CANFD_MAX_DLEN, MESSAGE_STANDARD | MESSAGE_FD_BRS);
                } else {
                    socketcan_write_data_can_fd(
                        INPUT_PARAMTER_ENABLE,
                        0x00, data, CANFD_MAX_DLEN, MESSAGE_STANDARD);
                }
                break; 
            case 'e':
            case 'E':
                socketcan_set_recv_filter();
                break;
            case 'f':
            case 'F':
                socketcan_clear_recv_filter();
                break;      
            case '?':
                print_cmd();
                break;
            default:
                printf("\n");
                break;
        }
    } while (cmd != 'q' && cmd != 'Q');

    return 0;
}

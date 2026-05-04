/**
 * @file main.c
 * @brief cmd list for test SocketCAN function
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2023, SUNIX Co., Ltd.
 *
 * Authors      : Max Chang <max.chang@sunix.com>
 * 
**/

#include "precomp.h"

#define DATE    "2023/01/16"
#define VERSION "0.0.1.0"

int  sd = -1;
char can_net_name[BUFF_LEN] = {0};

struct sockaddr_can gcan_addr;
int revthd_create_flag = 0;
pthread_t revthd_hd = 0;

static void print_info(void)
{
    printf("---------------------------------------\n");
    printf(" Date        : %s\n", DATE);
    printf(" Version     : %s\n", VERSION);
    printf("---------------------------------------\n");
}

static void print_cmd(void)
{
    printf(">-------------------------------------<\n");
    printf("    A : J1939 Socket Open \n");
    printf("    B : J1939 Socket Close \n");
    printf("    C : Send J1939 Message \n");
    printf("    D : Receive J1939 Message \n");

    printf("\n");
    printf("    ? : Show cmd\n");
    printf("    Q : quit\n");
    printf(">-------------------------------------<\n\n");

}

int main(void)
{
    int cmd,ret_n = 0;
   
    system("clear");
    print_info();
    printf("=========================================================================================\n");
    printf("|                              SocketCAN J1939 S/R Example                              |\n");
    printf("=========================================================================================\n");
    print_cmd();

    do{
        printf("d[#_#]b > CMD ?\n");
        cmd = sgetche();
        printf("\n");


        switch (cmd)
        {
            case 'a':
            case 'A':
                ret_n = J1939_open();
                
                if (ret_n >= 0) {
                    printf("#INFO: J1939 net device open Success\n");
                } 
                else {
                    printf("#ERROR: J1939 net device open Fail\n");
                }

                break;
            case 'b':
            case 'B':
                ret_n = J1939_close();

                if (ret_n >= 0) {
                    printf("#INFO: J1939 net device close Success\n");
                } 
                else {
                    printf("#ERROR: J1939 net device close Fail\n");
                }

                break;
            case 'c':
            case 'C':
                
                Send_J1939_message();

                break;
            case 'd':
            case 'D':

                if(sd > 0)
                {
                    int result = revthd_create();
                    if( result < 0) {
                        close(sd);
                        memset(can_net_name, 0x00, sizeof(char) * BUFF_LEN);
                        sd = -1;
                        printf("#INFO:J1939 receive thread fail\n");	        
                    }
                    else if (result == 0){
                        printf("#INFO:J1939 receive thread starting...\n");
                    }
                    else{

                    }
                }
                else
                {
                    printf("#Error: J1939 Socket device is not opened\n");
                }

                break;
            case '?':
                print_cmd();
                break;     
            default:
                printf("\n");
                break;
        }

    }while (cmd != 'q' && cmd != 'Q');
}


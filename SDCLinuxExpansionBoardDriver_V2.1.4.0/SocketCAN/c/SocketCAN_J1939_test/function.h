

#ifndef _J1939_PROG_SXXF_H_
#define _J1939_PROG_SXXF_H_

int input_can_net_device_name(char* name);

unsigned int input_J1939_address(int addr_type);

unsigned int input_J1939_pgn(void);

unsigned char input_J1939_msg_data(int index);

void print_l(int button);

char sgetche(void);

int J1939_open();

int J1939_close();

int Send_J1939_message();

int revthd_create();

void revthd_terminate();

#endif
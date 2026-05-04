/**
 * @file socketcan_func_test.c
 * @brief SocketCAN function
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

/*****************************************************************************************
 SocketCAN receive function
*****************************************************************************************/

// decode error frame
// notice : the following error condtion is defined in linux/can/error.h
// Depend on driver implementation, the error condition occurs will different  
static void can_error_frame_decode(
	unsigned int 	can_id,
	unsigned char*	data)
{
	if (can_id & CAN_ERR_TX_TIMEOUT) {
		printf("#INFO: Got CAN_ERR_TX_TIMEOUT\n");
	}
	if (can_id & CAN_ERR_LOSTARB) {
		printf("#INFO: Got CAN_ERR_LOSTARB(lost arbitration)\n");
	}
	if (can_id & CAN_ERR_CRTL) {
		printf("#INFO: Got CAN_ERR_CRTL\n");
		/* error status of CAN-controller / data[1] */
		printf("#INFO: controller problems, error:0x%.2x\n", data[1]);
		if (data[1] & CAN_ERR_CRTL_RX_OVERFLOW) {
			printf("#INFO: RX buffer overflow\n");
		}
		if (data[1] & CAN_ERR_CRTL_TX_OVERFLOW) {
			printf("#INFO: TX buffer overflow\n");
		}
		if (data[1] & CAN_ERR_CRTL_RX_WARNING) {
			printf("#INFO: reached warning level for RX errors\n");
		}
		if (data[1] & CAN_ERR_CRTL_TX_WARNING) {
			printf("#INFO: reached warning level for TX errors\n");
		}
		if (data[1] & CAN_ERR_CRTL_RX_PASSIVE) {
			printf("#INFO: reached error passive status RX\n");
		}
		if (data[1] & CAN_ERR_CRTL_TX_PASSIVE) {
			printf("#INFO: reached error passive status TX\n");
		}
		if (data[1] & CAN_ERR_CRTL_ACTIVE) {
			printf("#INFO: recovered to error active state\n");
		}
	}
	if (can_id & CAN_ERR_PROT) {
		printf("#INFO: Got CAN_ERR_PROT\n");
		/* error in CAN protocol (type) / data[2] */
		printf("#INFO: protocol violations, type error:0x%.2x\n", data[2]);
		if (data[2] & CAN_ERR_PROT_BIT) {
			printf("#INFO: single bit error\n");
		}
		if (data[2] & CAN_ERR_PROT_FORM) {
			printf("#INFO: frame format error\n");
		}
		if (data[2] & CAN_ERR_PROT_STUFF) {
			printf("#INFO: bit stuffing error\n");
		}
		if (data[2] & CAN_ERR_PROT_BIT0) {
			printf("#INFO: unable to send dominant bit\n");
		}
		if (data[2] & CAN_ERR_PROT_BIT1) {
			printf("#INFO: unable to send recessive bit\n");
		}
		if (data[2] & CAN_ERR_PROT_OVERLOAD) {
			printf("#INFO: bus overload\n");
		}
		if (data[2] & CAN_ERR_PROT_ACTIVE) {
			printf("#INFO: active error announcement\n");
		}
		if (data[2] & CAN_ERR_PROT_TX) {
			printf("#INFO: error occurred on transmission\n");
		}
		/* error in CAN protocol (location) / data[3] */
		printf("#INFO: protocol violations, location error:0x%.2x\n", data[3]);
		switch (data[3]) {
			case CAN_ERR_PROT_LOC_SOF:
				printf("#INFO: start of frame\n");
				break;
			case CAN_ERR_PROT_LOC_ID28_21:
				printf("#INFO: ID bits 28 - 21 (SFF: 10 - 3)\n");
				break;
			case CAN_ERR_PROT_LOC_ID20_18:
				printf("#INFO: ID bits 20 - 18 (SFF: 2 - 0 ) \n");
				break;
			case CAN_ERR_PROT_LOC_SRTR:
				printf("#INFO: substitute RTR (SFF: RTR)\n");
				break;
			case CAN_ERR_PROT_LOC_IDE:
				printf("#INFO: identifier extension\n");
				break;
			case CAN_ERR_PROT_LOC_ID17_13:
				printf("#INFO: ID bits 17-13\n");
				break;
			case CAN_ERR_PROT_LOC_ID12_05:
				printf("#INFO: ID bits 12-5\n");
				break;
			case CAN_ERR_PROT_LOC_ID04_00:
				printf("#INFO: ID bits 4-0\n");
				break;
			case CAN_ERR_PROT_LOC_RTR:
				printf("#INFO: RTR\n");
				break;
			case CAN_ERR_PROT_LOC_RES1:
				printf("#INFO: reserved bit 1\n");
				break;
			case CAN_ERR_PROT_LOC_RES0:
				printf("#INFO: reserved bit 0\n");
				break;
			case CAN_ERR_PROT_LOC_DLC:
				printf("#INFO: data length code\n");
				break;
			case CAN_ERR_PROT_LOC_DATA:
				printf("#INFO: data section\n");
				break;
			case CAN_ERR_PROT_LOC_CRC_SEQ:
				printf("#INFO: CRC sequence\n");
				break;
			case CAN_ERR_PROT_LOC_CRC_DEL:
				printf("#INFO: CRC delimiter\n");
				break;
			case CAN_ERR_PROT_LOC_ACK:
				printf("#INFO: ACK slot\n");
				break;
			case CAN_ERR_PROT_LOC_ACK_DEL:
				printf("#INFO: ACK delimiter\n");
				break;
			case CAN_ERR_PROT_LOC_EOF:
				printf("#INFO: end of frame\n");
				break;
			case CAN_ERR_PROT_LOC_INTERM:
				printf("#INFO: intermission\n");
				break;
			default:
				break;
		}		
	}
	if (can_id & CAN_ERR_TRX) {
		printf("#INFO: Got CAN_ERR_TRX\n");
		/* error status of CAN-transceiver / data[4] */
		printf("#INFO: transceiver status, error:0x%.2x\n", data[4]);
		switch (data[4]) {
			case CAN_ERR_TRX_CANH_NO_WIRE:
				printf("#INFO: CAN_ERR_TRX_CANH_NO_WIRE\n");
				break;
			case CAN_ERR_TRX_CANH_SHORT_TO_BAT:
				printf("#INFO: CAN_ERR_TRX_CANH_SHORT_TO_BAT\n");
				break;
			case CAN_ERR_TRX_CANH_SHORT_TO_VCC:
				printf("#INFO: CAN_ERR_TRX_CANH_SHORT_TO_VCC\n");
				break;
			case CAN_ERR_TRX_CANH_SHORT_TO_GND:
				printf("#INFO: CAN_ERR_TRX_CANH_SHORT_TO_GND\n");
				break;
			case CAN_ERR_TRX_CANL_NO_WIRE:
				printf("#INFO: CAN_ERR_TRX_CANL_NO_WIRE\n");
				break;
			case CAN_ERR_TRX_CANL_SHORT_TO_BAT:
				printf("#INFO: CAN_ERR_TRX_CANL_SHORT_TO_BAT\n");
				break;
			case CAN_ERR_TRX_CANL_SHORT_TO_VCC:
				printf("#INFO: CAN_ERR_TRX_CANL_SHORT_TO_VCC\n");
				break;
			case CAN_ERR_TRX_CANL_SHORT_TO_GND:
				printf("#INFO: CAN_ERR_TRX_CANL_SHORT_TO_GND\n");
				break;
			case CAN_ERR_TRX_CANL_SHORT_TO_CANH:
				printf("#INFO: CAN_ERR_TRX_CANL_SHORT_TO_CANH\n");
				break;
			default:
				break;
		}		
	}
	if (can_id & CAN_ERR_ACK) {
		printf("#INFO: Got CAN_ERR_ACK\n");
	}
	if (can_id & CAN_ERR_BUSOFF) {
		printf("#INFO: Got CAN_ERR_BUSOFF\n");
	}
	if (can_id & CAN_ERR_RESTARTED) {
		printf("#INFO: Got CAN_ERR_RESTARTED\n");
	}
}

/*-----------------------------------------------------------------------------*/

// recevie thread work function
static void *rxthd(
	void *context)
{
    unsigned int can_id = 0;
    int i = 0, size = 0, nbytes = 0;
	struct pollfd pfd;
	void* can_frame = NULL;
	struct canfd_frame frame;

	can_frame = &frame;
	size = sizeof(struct canfd_frame);

	printf("#INFO: %s is waiting for CAN message\n", can_net_name);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	pfd.fd = sd;
	pfd.events = (POLLIN | POLLRDNORM);

	while (1) {
		poll(&pfd, 1, -1);
		if ((pfd.revents & POLLIN) == POLLIN) {
			
			nbytes = 0;
			nbytes = read(sd,  can_frame, size);
			if (nbytes == CANFD_MTU) {
				printf("%s read data size=CANFD_MTU:%ld\n", can_net_name, CANFD_MTU);
				printf("-----%s, Receiving CAN FD message Start-----\n", can_net_name);      
				// got error frame
				if ((((struct canfd_frame*)can_frame)->can_id & CAN_ERR_FLAG) == CAN_ERR_FLAG) {
					printf("#INFO: Got Error frame\n");
					can_id = ((struct canfd_frame*)can_frame)->can_id & CAN_ERR_MASK;
					printf("#INFO: Id=0x%X Length=%d\n", can_id, ((struct canfd_frame*)can_frame)->len);
					can_error_frame_decode(can_id, ((struct canfd_frame*)can_frame)->data);
				} else {
					if ((((struct canfd_frame*)can_frame)->can_id & CAN_EFF_FLAG) == CAN_EFF_FLAG) {
						can_id = ((struct canfd_frame*)can_frame)->can_id & CAN_EFF_MASK;
						printf("#INFO: Got Extended frame\n");
					} else {
						printf("#INFO: Got Standard frame\n");
						can_id = ((struct canfd_frame*)can_frame)->can_id & CAN_SFF_MASK;
					}
					printf("#INFO: Id=0x%X Length=%d\n", can_id, ((struct canfd_frame*)can_frame)->len);
					if ((((struct canfd_frame*)can_frame)->flags & CANFD_BRS) == CANFD_BRS) {
						printf("#INFO: Got FD_BRS\n");
					}
					if ((((struct canfd_frame*)can_frame)->flags & CANFD_ESI) == CANFD_ESI) {
						printf("#INFO: Got FD_ESI\n");
					}	                 
					if (((struct canfd_frame*)can_frame)->len > 0) {
						printf("Data:\n");	
						for (i=1; i<=((struct canfd_frame*)can_frame)->len; i++) {
							printf("x%02x ", ((struct canfd_frame*)can_frame)->data[i-1]);
							if( i % 8 == 0)
								printf("\n");
						}
						printf("\n");
					}					
				}
				printf("-----%s, Receiving CAN FD message End-------\n", can_net_name); 
			} else if(nbytes == CAN_MTU) {
				printf("%s read data size=CAN_MTU:%ld\n", can_net_name, CAN_MTU);
				printf("-----%s, Receiving CAN message Start-----\n", can_net_name);      
				// got error frame
				if ((((struct can_frame*)can_frame)->can_id & CAN_ERR_FLAG) == CAN_ERR_FLAG) {
					printf("#INFO: Got Error frame\n");
					can_id = ((struct can_frame*)can_frame)->can_id & CAN_ERR_MASK;
					printf("#INFO: Id=0x%X Length=%d\n", 
							can_id, ((struct can_frame*)can_frame)->can_dlc);
					can_error_frame_decode(can_id, ((struct can_frame*)can_frame)->data);
				} else {
					if ((((struct can_frame*)can_frame)->can_id & CAN_EFF_FLAG) == CAN_EFF_FLAG) {
						can_id = ((struct can_frame*)can_frame)->can_id & CAN_EFF_MASK;
						printf("#INFO: Got Extended frame\n");
					} else {
						printf("#INFO: Got Standard frame\n");
						can_id = ((struct can_frame*)can_frame)->can_id & CAN_SFF_MASK;
					}
					printf("#INFO: Id=0x%X Length=%d\n", can_id, ((struct can_frame*)can_frame)->can_dlc);
					if ((((struct can_frame*)can_frame)->can_id & CAN_RTR_FLAG) == CAN_RTR_FLAG) {
						printf("#INFO: Got RTR(remote transmission request)\n");
					} 
					if (((struct can_frame*)can_frame)->can_dlc > 0) {
						printf("Data:\n");
						for (i=1; i<=((struct can_frame*)can_frame)->can_dlc; i++) {
							printf("x%02x ", ((struct can_frame*)can_frame)->data[i-1]);
							if( i % 8 == 0)
								printf("\n");
						}
						printf("\n");
					}				
				}
				printf("-----%s, Receiving CAN message End-------\n", can_net_name); 
			} else {
				printf("%s read fail, errno:%s\n", can_net_name, strerror(errno));
			}		
		}
	}

	rxthd_create_flag = 0;
	printf("#INFO: %s, %s, exit\n", can_net_name, __func__);
	pthread_exit(NULL);
}

/*-----------------------------------------------------------------------------*/

// create receive thread
static int rxthd_create()
{
	int ret_n = 0;
	if (rxthd_create_flag) {
		print_l(0);
		printf("#WARN: %s rx thd created\n", can_net_name);
		print_l(1);
		return -1;
	}

	ret_n = pthread_create(&rxthd_hd, NULL, rxthd, NULL);
	if (ret_n < 0) {
		print_l(0);
		printf("#ERROR: %s rx thd create fail, errno %d\n", can_net_name, errno);
		print_l(1);
		return -1;
	}

	rxthd_create_flag = 1;
	print_l(0);
	printf("#INFO: %s rx thd create ok\n", can_net_name);
	print_l(1);

    return 0;
}

/*-----------------------------------------------------------------------------*/

// terminate receive thread
static void rxthd_terminate()
{
	if (!rxthd_create_flag) {
		print_l(0);
		printf("#WARN: %s rx thd not create\n", can_net_name);
		print_l(1);
		return;
	}

	pthread_cancel(rxthd_hd);
	pthread_join(rxthd_hd, NULL);
	rxthd_create_flag = 0;
	print_l(0);
	printf("#INFO: %s rx thd terminate ok\n", can_net_name);
	print_l(1);
}

/*****************************************************************************************
 SocketCAN open/close function
*****************************************************************************************/

// check whether device is open or not
static int check_device_open()
{
	if (sd > 0) {
		print_l(0);
        printf("#WARN: Some net device is opened, is %s\n", can_net_name);
		print_l(1);
		return -1;
	}
	return 0;
}

/*-----------------------------------------------------------------------------*/

// open socket, thread and do relative actions
int socketcan_open()
{
    int ret_n = 0;
    char name[BUFF_LEN];
    struct sockaddr_can addr;
    struct ifreq ifr;
    memset( name, 0x00, sizeof(name));

    ret_n = input_can_net_device_name(name);
    if (ret_n < 0) {
        return ret_n;
    }

    if (check_device_open() < 0) {
        return -1;
    }

	// Create socket
	if( (sd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0 ) {
		printf("#ERROR: Create socket failed\n");
        return -1;
	}

	// Set can name
	strcpy(ifr.ifr_name, name);
	ioctl(sd, SIOCGIFINDEX, &ifr);
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	// check the net device is running or not
	if (ioctl(sd, SIOCGIFFLAGS, &ifr) < 0) {
		printf("#ERROR: ioctl SIOCGIFFLAGS fail, errno:%s\n", strerror(errno));
		close(sd);
        sd = -1;
		return -1;
	} else {
		if (ifr.ifr_ifru.ifru_flags & IFF_RUNNING) {
			printf("#INFO: %s is running\n", name);
		} else {
			printf("#ERROR: %s is not running(make sure your net device state is up))\n", name);
			close(sd);
			sd = -1;
			return -1;
		}
	}	
	// check if the frame fits into the CAN net device
	if (ioctl(sd, SIOCGIFMTU, &ifr) < 0) {
		printf("#ERROR: ioctl SIOCGIFMTU fail, errno:%s\n", strerror(errno));
		close(sd);
        sd = -1;
		return -1;
	}
	// check mtu
	if (ifr.ifr_mtu == CANFD_MTU) {
		can_type = TYPE_CAN_FD;
		printf("#INFO: CAN_type: CAN FD\n");
		// enable CAF FD frames
		int enable_canfd = 1;
		if (setsockopt(sd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd, sizeof(enable_canfd)) < 0) {
			printf("#ERROR: error when enabling CAN FD support, errno:%s\n", strerror(errno));
			close(sd);
       		sd = -1;
			return -1;
		}
	} else {
		can_type = TYPE_CAN;
		printf("#INFO: CAN_type: CAN\n");
	}	

#if (ENABLE_ALL_CAN_ERROR_MASKS == 1)
	can_err_mask_t err_mask =
		( CAN_ERR_TX_TIMEOUT   /* TX timeout (by netdevice driver) */
		| CAN_ERR_LOSTARB      /* lost arbitration    / data[0]    */
		| CAN_ERR_CRTL         /* controller problems / data[1]    */
		| CAN_ERR_PROT         /* protocol violations / data[2..3] */
		| CAN_ERR_TRX          /* transceiver status  / data[4]    */
		| CAN_ERR_ACK          /* received no ACK on transmission  */
		| CAN_ERR_BUSOFF       /* bus off */
		| CAN_ERR_RESTARTED    /* controller restarted */
    );
#else 
	// only enable some error masks
	can_err_mask_t err_mask =
		( CAN_ERR_TX_TIMEOUT   /* TX timeout (by netdevice driver) */
		| CAN_ERR_CRTL         /* controller problems / data[1]    */
		| CAN_ERR_BUSOFF       /* bus off */
		| CAN_ERR_RESTARTED    /* controller restarted */
    );
#endif
	// set error masks
	if (setsockopt(sd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask)) < 0) {
		printf("#ERROR: error when set error filter, errno:%s\n", strerror(errno));
		close(sd);
        sd = -1;
		return -1;
	}
	// bind
	if(bind(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ) {
        printf("#ERROR: Bind failed\n");
		close(sd);
        sd = -1;
		return -1;
	} else {
        memset(can_net_name, 0x00, sizeof(char) * BUFF_LEN);
        memcpy(can_net_name, name, strlen(name));
    }

    if( rxthd_create() < 0) {
        close(sd);
        memset(can_net_name, 0x00, sizeof(char) * BUFF_LEN);
        sd = -1;
		return -1;
    }

	print_l(0);
	printf("#INFO: %s open ok, sd %d\n", name, sd);
	print_l(1);

	memcpy(&gcan_addr, &addr, sizeof(struct sockaddr_can));

    return ret_n;
}

/*-----------------------------------------------------------------------------*/

// close socket and thread
int socketcan_close()
{
    if (sd < 0) {
		print_l(0);
        printf("#INFO: No net device is opened\n");
		print_l(1);
		return -1;
	}
    
    rxthd_terminate();

	close(sd);
    sd = -1;
	print_l(0);
    printf("#INFO: %s close ok\n", can_net_name);	
	print_l(1);

    return 0;
}

/*****************************************************************************************
 SocketCAN filter function
*****************************************************************************************/

// set receive filter
int socketcan_set_recv_filter()
{	
	int i = 0, frame_type = 0, filter_amount = 0;
	if (sd < 0) {
		print_l(0);
        printf("#INFO: No net device is opened\n");
		print_l(1);
		return -1;
	}
	// release filter memory
	if (prfilter != NULL) {
		free(prfilter);
		prfilter = NULL;
	}
	filter_amount = input_id_filter_amount();
	prfilter = (struct can_filter*)malloc( sizeof(struct can_filter) * filter_amount);
	for (i=1; i<=filter_amount; i++) {
		printf( "#%d id message filter\n", i);
		prfilter[i-1].can_id = input_msg_id();
		frame_type = input_id_filter_frame_type();
		if (frame_type == MESSAGE_STANDARD) {
			prfilter[i-1].can_mask = CAN_SFF_MASK;
		} else {
			prfilter[i-1].can_mask = CAN_EFF_MASK;
		}
	}
	if (setsockopt(sd, SOL_CAN_RAW, CAN_RAW_FILTER, prfilter, sizeof(struct can_filter) * filter_amount) < 0) {
        printf("#ERROR: %s Set receive filter fail, errno:%s\n", can_net_name, strerror(errno));
		free(prfilter);
		prfilter = NULL;
    } else {
        printf("#INFO: %s Set receive filter success, file amount:%d\n", can_net_name, filter_amount);
		for (i=1; i<=filter_amount; i++) {
			printf("-----#%d id message filter-----\n", i);
			printf("id: x%08x\n", prfilter[i-1].can_id);
			if (prfilter[i-1].can_mask == CAN_SFF_MASK) {
				printf("frame_type: Standard(0)\n");
			} else {
				printf("frame_type: Extended(1)\n");
			}
			printf("-----#%d id message filter-----\n", i);
		}
    }

	return 0;
}

/*-----------------------------------------------------------------------------*/

// clear receive filter
int socketcan_clear_recv_filter()
{
	struct can_filter rfilter;
	rfilter.can_id = 0x00;
	rfilter.can_mask = 0x00;

	if (sd < 0) {
		print_l(0);
        printf("#INFO: No net device is opened\n");
		print_l(1);
		return -1;
	}
	// release filter memory
	if (prfilter != NULL) {
		free(prfilter);
		prfilter = NULL;
	}
	if (setsockopt(sd, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter)) < 0) {
	    printf("#ERROR: %s Clear receive filter fail, errno:%s\n", can_net_name, strerror(errno));
		return -1;
    } else {
        printf("#INFO: %s Clear receive filter success\n", can_net_name);
    } 

	return 0;
}

/*****************************************************************************************
 SocketCAN write function
*****************************************************************************************/

// CAN DLC to real data length conversion helpers
static const unsigned char dlc2len[] = {
					0, 1, 2, 3, 4, 5, 6, 7,
					8, 12, 16, 20, 24, 32, 48, 64};

// get data length from raw data length code (DLC)
static unsigned char can_fd_dlc2len(
	unsigned char dlc)
{
	printf( "dlc2len[dlc & 0x0F]:%d, dlc2len[dlc]:%d\n", dlc2len[dlc & 0x0F], dlc2len[dlc]);
	return dlc2len[dlc & 0x0F];
}

static const unsigned char len2dlc[] = {
					0, 1, 2, 3, 4, 5, 6, 7, 8,			/* 0 - 8 */
					9, 9, 9, 9,							/* 9 - 12 */
					10, 10, 10, 10,						/* 13 - 16 */
					11, 11, 11, 11,						/* 17 - 20 */
					12, 12, 12, 12,						/* 21 - 24 */
					13, 13, 13, 13, 13, 13, 13, 13,		/* 25 - 32 */
					14, 14, 14, 14, 14, 14, 14, 14,		/* 33 - 40 */
					14, 14, 14, 14, 14, 14, 14, 14,		/* 41 - 48 */
					15, 15, 15, 15, 15, 15, 15, 15,		/* 49 - 56 */
					15, 15, 15, 15, 15, 15, 15, 15};	/* 57 - 64 */

// map the sanitized data length to an appropriate data length code
static unsigned char can_fd_len2dlc(
	unsigned char len)
{
	if (len > CANFD_MAX_DLEN)
		return 0xF;

	return len2dlc[len];
}

/*-----------------------------------------------------------------------------*/

//  write CAN message
int socketcan_write_data_can(
    int is_input_para,
    unsigned int id,
    unsigned char *data,
    int data_len,
    int message_type_flags)
{
    int i = 0;
	struct can_frame can_frame;

	if (sd < 0) {
		print_l(0);
        printf("#INFO: No net device is opened\n");
		print_l(1);
		return -1;
	}

    if (is_input_para == INPUT_PARAMTER_ENABLE) {
		while(1) {
			id = input_msg_id();
			if (id > CAN_EFF_MASK) {
				printf("#ERROR: Input ID value is too large\n");
				continue;
			}
			message_type_flags = input_msg_type();
			if ((message_type_flags & MESSAGE_EXTENDED) == MESSAGE_EXTENDED) {
				id |= CAN_EFF_FLAG;
			}
			if ((message_type_flags & MESSAGE_RTR) == MESSAGE_RTR) {
				id |= CAN_RTR_FLAG;
			}
			if ((message_type_flags & MESSAGE_FD_BRS) == MESSAGE_FD_BRS) {
				printf("#ERROR: Not support CAN FD attribute\n");
				continue;
			}
			data_len = input_msg_len_n();
			if ((message_type_flags & MESSAGE_RTR) == MESSAGE_RTR) {
				data_len = 0;
				memset(data, 0x00, BUFF_LEN);
			}
			if ( data_len > CAN_MAX_DLC) {
				printf("#ERROR: Input data length value is too large\n");
				continue;
			}
			// random data
			if (input_msg_data_random_enable() == RANDOM_DATA_ENABLE) {
				time_t t;
				srand((unsigned)time(&t));
				for (i = 0; i < data_len; i++) {
					data[i] = rand() % 256;
				}
			} else {
				for (i = 0; i < data_len; i++) {
					data[i] = input_msg_data();
				}
			}
			break;
		}	
    }

	can_frame.can_id = id;									// can id
	can_frame.can_dlc = (unsigned char)data_len;	        // data length
	for (i = 0; i < can_frame.can_dlc; i++) {				// data
		can_frame.data[i] = data[i];
	}
	printf("-----%s, Show write CAN message Start-----\n", can_net_name);
	printf("id       : x%08x\n", can_frame.can_id);
	printf("len      : %d\n", can_frame.can_dlc);			
	if (can_frame.can_dlc > 0) {
		printf("-----data start-----\n");
		for (i = 1; i <= can_frame.can_dlc; i++) {
			printf("x%02x ", can_frame.data[i-1]);
			if( i % 8 == 0)
				printf("\n");
		}
		printf("\n-----data end-------\n");
	}
	printf("-----%s, Show write CAN message End-------\n", can_net_name);
	if (write(sd, &can_frame, sizeof(struct can_frame)) < 0) {
        printf("#ERROR: %s write failed, errno:%s\n", can_net_name, strerror(errno));
		return -1;
	} else {
		printf("#INFO: %s write success\n", can_net_name);
	}

    return 0;
}

/*-----------------------------------------------------------------------------*/

// write CAN FD message
int socketcan_write_data_can_fd(
    int is_input_para,
    unsigned int id,
    unsigned char *data,
    int data_len,
    int message_type_flags)
{
    int i = 0;
	struct canfd_frame can_frame;
	unsigned char fd_flags = 0x00;

	if (sd < 0) {
		print_l(0);
        printf("#INFO: No net device is opened\n");
		print_l(1);
		return -1;
	}

	if (can_type == TYPE_CAN_FD) {
		// do nothing
	} else {
		printf("#ERROR: Unsupport operation for write FD messsage\n");
		return -1;
	}

    if (is_input_para == INPUT_PARAMTER_ENABLE) {
		while(1) {
			fd_flags = 0x00;
			id = input_msg_id();
			if (id > CAN_EFF_MASK) {
				printf("#ERROR: Input ID value is too large\n");
				continue;
			}
			message_type_flags = input_msg_type();
			if ((message_type_flags & MESSAGE_EXTENDED) == MESSAGE_EXTENDED) {
				id |= CAN_EFF_FLAG;
			}
			if ((message_type_flags & MESSAGE_FD_BRS) == MESSAGE_FD_BRS) {
				fd_flags |= CANFD_BRS;
			}
			if ((message_type_flags & MESSAGE_RTR) == MESSAGE_RTR) {
				printf("#ERROR: Not support CAN RTR attribute\n");
				continue;
			}
			data_len = input_msg_len_n();
			if ( data_len > CANFD_MAX_DLEN) {
				printf("#ERROR: Input data length value is too large\n");
				continue;
			}
			// random data
			if (input_msg_data_random_enable() == RANDOM_DATA_ENABLE) {
				time_t t;
				srand((unsigned)time(&t));
				for (i = 0; i < data_len; i++) {
					data[i] = rand() % 256;
				}
			} else {
				for (i = 0; i < data_len; i++) {
					data[i] = input_msg_data();
				}
			}
			// ensure discrete CAN FD length values 0..8, 12, 16, 20, 24, 32, 64
			data_len = can_fd_dlc2len(can_fd_len2dlc(data_len));
			break;
		}	
    } else {
		if ((message_type_flags & MESSAGE_EXTENDED) == MESSAGE_EXTENDED) {
			id |= CAN_EFF_FLAG;
		}
		if ((message_type_flags & MESSAGE_FD_BRS) == MESSAGE_FD_BRS) {
			fd_flags |= CANFD_BRS;
		}
		// ensure discrete CAN FD length values 0..8, 12, 16, 20, 24, 32, 64
		data_len = can_fd_dlc2len(can_fd_len2dlc(data_len));
	}

	can_frame.can_id = id;								// can id
	can_frame.flags = fd_flags;							// fd flags
	can_frame.len = (unsigned char)data_len;	        // data length
	for (i = 0; i < can_frame.len; i++) {				// data
		can_frame.data[i] = data[i];
	}
	printf("-----%s, Show write CAN FD message Start-----\n", can_net_name);
	printf("id       : x%08x\n", can_frame.can_id);
	printf("FD flags : %d\n", can_frame.flags);	
	printf("len      : %d\n", can_frame.len);			
	if (can_frame.len > 0) {
		printf("-----data start-----\n");
		for (i = 1; i <= can_frame.len; i++) {
			printf("x%02x ", can_frame.data[i-1]);
			if( i % 8 == 0)
				printf("\n");
		}
		printf("\n-----data end-------\n");
	}
	printf("-----%s, Show write CAN FD message End-------\n", can_net_name);
	if (write(sd, &can_frame, sizeof(struct canfd_frame)) < 0) {
        printf("#ERROR: %s write failed, errno:%s\n", can_net_name, strerror(errno));
		return -1;
	} else {
		printf("#INFO: %s write success\n", can_net_name);	
	}

    return 0;
}

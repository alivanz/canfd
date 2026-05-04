#include "precomp.h"

//J1939 recevie thread work function
static void *J1939_revthd()
{
    struct pollfd pfd;
    char buf[BUFF_LEN];

    int len = 0;

    printf("#INFO: %s is waiting for J1939 message\n", can_net_name);

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    pfd.fd = sd;
    pfd.events = (POLLIN | POLLRDNORM);

    socklen_t addr_len = sizeof(gcan_addr);
    
    while (1)
    {
        poll(&pfd, 1, -1);

        if ((pfd.revents & POLLIN) == POLLIN) {
                       
           len = 0;
           len = recvfrom(sd, buf, sizeof(buf), MSG_DONTWAIT, (struct sockaddr *)&gcan_addr, &addr_len);
            
            if(len < 0){
                continue;
            }
            
            printf("===========Received J1939 Message===========\n");
            
            printf("Address: 0x%02X\n", gcan_addr.can_addr.j1939.addr);
            printf("PGN: 0x%X\n", gcan_addr.can_addr.j1939.pgn);
            printf("Data:");

            for(int i = 0; i < len; i++)
            {
                printf("0x%02X ",(unsigned char)buf[i]);
            }

            printf("\n");
            printf("=============================================\n");


                   
        }
    }   
    pthread_exit(NULL);
}

// create J1939 receive thread
int revthd_create()
{
    int ret_n = 0;
	if (revthd_create_flag) {
		print_l(0);
		printf("#WARN: %s J1939 receive thread is created\n", can_net_name);
		print_l(1);
		return 1;
	}

	ret_n = pthread_create(&revthd_hd, NULL, J1939_revthd, NULL);
	if (ret_n < 0) {
		print_l(0);
		printf("#ERROR: %s J1939 receive thread creat fail, errno %d\n", can_net_name, errno);
		print_l(1);
		return -1;
	}

	revthd_create_flag = 1;
	print_l(0);
	printf("#INFO: %s J1939 receive thread create ok\n", can_net_name);
	print_l(1);

    return 0;
}

// terminate receive thread
void revthd_terminate()
{
	if (!revthd_create_flag) {
		print_l(0);
		printf("#WARN: %s J1939 receive thread not create\n", can_net_name);
		print_l(1);
		return;
	}

	pthread_cancel(revthd_hd);
	pthread_join(revthd_hd, NULL);
	revthd_create_flag = 0;
	print_l(0);
	printf("#INFO: %s J1939 receive thread terminate ok\n", can_net_name);
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

int J1939_open()
{
    int ret_n = 0;
    char name[BUFF_LEN];
    struct sockaddr_can baddr;
    struct ifreq ifr;
    unsigned char sa = 0;
    int addr_type = J1939_SA;
    int brocast_enable = 1;

    memset( name, 0x00, sizeof(name));

    ret_n = input_can_net_device_name(name);
    
    if (ret_n < 0) {
        return ret_n;
    }

    if (check_device_open() < 0) {
        return -1;
    }

    // Create J1939 socket
	if( (sd = socket(PF_CAN, SOCK_DGRAM, CAN_J1939)) < 0 ) {
		printf("#ERROR: Create J1939 socket failed, Error Code: %d\n", sd);
        return -1;
	}
    
    // Set can name
    strcpy(ifr.ifr_name, name);
    ioctl(sd, SIOCGIFINDEX, &ifr);

    /************************************************/
    /**29 bit extended CAN-ID************************/
    /**[Priority]   [PGN]       [SA(Source Address)]*/
    /**[Bit 28...26][Bit 25...8][Bit 7...0]**********/
    /************************************************/
    
    sa= input_J1939_address(addr_type);

    baddr.can_family = AF_CAN;
    baddr.can_ifindex = ifr.ifr_ifindex;
    baddr.can_addr.j1939.name = J1939_NO_NAME;
    baddr.can_addr.j1939.pgn = J1939_NO_PGN;
    baddr.can_addr.j1939.addr = sa; /*J1939 SA*/

    if(ioctl(sd, SIOCGIFFLAGS, &ifr) < 0) {
        printf("#ERROR: ioctl SIOCGIFFLAGS fail, errno:%s\n", strerror(errno));
		close(sd);
        sd = -1;
		return -1;
    }
    else
    {
        if (ifr.ifr_ifru.ifru_flags & IFF_RUNNING) {
		    printf("#INFO: %s is running\n", name);
		} 
        else {
			printf("#ERROR: %s is not running(make sure your net device state is up))\n", name);
			close(sd);
			sd = -1;
			return -1;
		}
    }

    // check if the frame fits into the CAN net device
	if (ioctl(sd, SIOCGIFMTU, &ifr) < 0){
		printf("#ERROR: ioctl SIOCGIFMTU fail, errno:%s\n", strerror(errno));
		close(sd);
        sd = -1;
		return -1;
	}

    //Set Broadcast
    ret_n = setsockopt(sd, SOL_SOCKET, SO_BROADCAST, &brocast_enable, sizeof(brocast_enable));
    if(ret_n < 0 ){
        printf("#ERROR: Set boradcast failed, Error Code: %d\n", ret_n);
    }
    else{
            printf("#INFO: Set boradcast \n");
    }
            
    /*Binding local address*/
    ret_n = bind(sd, (struct sockaddr *)&baddr, sizeof(baddr));
    if(ret_n < 0){
        printf("#ERROR: Bind failed, Error Code: %d\n", ret_n);
		close(sd);
        sd = -1;
		return -1;
    }
    else{
        memset(can_net_name, 0x00, sizeof(char) * BUFF_LEN);
        memcpy(can_net_name, name, strlen(name));
    }

    print_l(0);
	printf("#INFO: %s open ok, sd %d\n", name, sd);
	print_l(1);

    memcpy(&gcan_addr, &baddr, sizeof(struct sockaddr_can));


    return ret_n;
}

int J1939_close()
{
    if (sd < 0) {
		print_l(0);
        printf("#INFO: No net device is opened\n");
		print_l(1);
		return -1;
	}

    revthd_terminate();
    
	close(sd);
    sd = -1;
	print_l(0);
    printf("#INFO: %s close ok\n", can_net_name);	
	print_l(1);

    return 0;
}

int Send_J1939_message()
{
    //int priority = 1;
    unsigned int inp_pgn = 0;
    unsigned char da = 0;

    if (sd < 0) {
        print_l(0);
        printf("#INFO: No net device is opened\n");
		print_l(1);
		return -1;
    }

    inp_pgn =  input_J1939_pgn();
    
    int addr_type = J1939_DA;

    da = input_J1939_address(addr_type);

    struct sockaddr_can saddr =
    {
        .can_family = AF_CAN,
        .can_addr.j1939= {
            .name = J1939_NO_NAME,
            .addr = da, /*Detination Address*/
            .pgn = inp_pgn
        }       
    };

    /*if(setsockopt(sd, SOL_CAN_J1939, SO_J1939_SEND_PRIO, &priority, sizeof(priority)) < 0){
        printf("#ERROR: Set J1939 priority failed\n");
        return -1;
    }*/
    
    int index = 7;
    unsigned char data[J1939_DATA_LEN];

    printf("Input J1939 Data(8byte):\n");
    for(int i = 0; i < J1939_DATA_LEN; i++)
    {   
        data[i] = input_J1939_msg_data(index);
        index--;
    }

    int result = sendto(sd, data, sizeof(data), 0, (const struct sockaddr *)&saddr, sizeof(saddr));

    if(result < 0){
        printf("#ERROR: Sendto failed, Error Code: %d\n", result);
        return -1;
    }

    return 0;
}
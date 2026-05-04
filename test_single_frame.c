/*
 * test_single_frame.c - Send ONE CAN-FD frame and exit (like cansend)
 *
 * Compile: gcc -O2 -Wall -o test_single_frame test_single_frame.c
 * Usage: ./test_single_frame can0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return 1;
    }

    const char *iface = argv[1];
    int sock = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX");
        close(sock);
        return 1;
    }

    int ifindex = ifr.ifr_ifindex;

    /* Check MTU and enable FD */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFMTU, &ifr) < 0) {
        perror("ioctl SIOCGIFMTU");
        close(sock);
        return 1;
    }

    if (ifr.ifr_mtu != CANFD_MTU) {
        fprintf(stderr, "MTU is %d, not CANFD_MTU (%d)\n", ifr.ifr_mtu, CANFD_MTU);
        close(sock);
        return 1;
    }

    int enable_canfd = 1;
    if (setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd, sizeof(enable_canfd)) < 0) {
        perror("setsockopt CAN_RAW_FD_FRAMES");
        close(sock);
        return 1;
    }

    /* Disable loopback */
    int loopback = 0;
    if (setsockopt(sock, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &loopback, sizeof(loopback)) < 0) {
        perror("setsockopt CAN_RAW_LOOPBACK");
        close(sock);
        return 1;
    }

    /* Bind */
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifindex;
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    /* Build frame */
    struct canfd_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id = 0x123;
    frame.len = 12;
    frame.flags = CANFD_BRS | CANFD_ESI;
    for (int i = 0; i < 12; i++)
        frame.data[i] = (unsigned char)i;

    fprintf(stderr, "Sending single CAN-FD frame: ID=0x123, len=12, flags=0x%02X, loopback=OFF\n",
            frame.flags);

    ssize_t nbytes = write(sock, &frame, CANFD_MTU);
    if (nbytes < 0) {
        perror("write");
        close(sock);
        return 1;
    }

    fprintf(stderr, "Write returned %zd bytes\n", nbytes);

    close(sock);
    return 0;
}

/*
 * net_writer.c - Write CAN/CANFD frames to a SocketCAN interface.
 *
 * Uses AF_CAN raw socket directly (no slcand, no ASCII protocol).
 *
 * Usage:
 *   ./net_writer --iface can0 [--fd] [--ext] [--id 0x100] [--len 8] [--rate 0]
 *
 * Compile:
 *   gcc -O2 -Wall -o net_writer net_writer.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <sched.h>
#include <sys/mman.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "cants.h"

static volatile sig_atomic_t g_running = 1;
static int g_sock = -1;

/* DLC value (0-F) → actual byte count for CAN FD */
static const int dlc_to_len[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8,   /* 0-8 */
    12, 16, 20, 24, 32, 48, 64    /* 9-F */
};

/* Byte count → smallest DLC that fits (CAN FD) */
static int len_to_dlc(int len)
{
    for (int i = 0; i < 16; i++) {
        if (dlc_to_len[i] >= len)
            return i;
    }
    return 15;
}

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static int can_open(const char *iface, int enable_fd)
{
    int sock = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX");
        close(sock);
        return -1;
    }

    int ifindex = ifr.ifr_ifindex;  /* Save before MTU query overwrites it */

    /* Check MTU and enable FD if needed (like cansend does) */
    if (enable_fd) {
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
        if (ioctl(sock, SIOCGIFMTU, &ifr) < 0) {
            perror("ioctl SIOCGIFMTU");
            close(sock);
            return -1;
        }
        if (ifr.ifr_mtu == CANFD_MTU) {
            int val = 1;
            if (setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &val, sizeof(val)) < 0) {
                perror("setsockopt CAN_RAW_FD_FRAMES");
                close(sock);
                return -1;
            }
        } else {
            fprintf(stderr, "Interface MTU is %d, not CANFD_MTU (%zu) - FD mode may not work\n",
                    ifr.ifr_mtu, CANFD_MTU);
        }
    }

    /* Disable default receive filter to save CPU (like cansend does) */
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifindex;
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }

    return sock;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s --iface <device> [options]\n"
        "\n"
        "Options:\n"
        "  --iface <dev>  CAN interface (e.g. can0, vcan0)  [required]\n"
        "  --fd           Use CAN FD frames (default: classic CAN)\n"
        "  --brs          Enable Bit Rate Switch (use data bitrate for payload)\n"
        "  --esi          Enable Error State Indicator\n"
        "  --ext          Use extended 29-bit ID (default: standard 11-bit)\n"
        "  --id <hex>     Arbitration ID in hex (default: 100)\n"
        "  --len <n>      Payload length in bytes (default: 8)\n"
        "  --rate <us>    Interval between frames in microseconds (default: 0 = full speed)\n"
        "  --timestamp    Embed send timestamp in payload (min 8 bytes, for latency testing)\n"
        "  --priority <n> SCHED_FIFO RT priority 1-99 (default: 80, 0 = disabled)\n"
        "  -h, --help     Show this help\n",
        prog);
}

int main(int argc, char *argv[])
{
    const char *iface = NULL;
    int use_fd = 0;
    int use_brs = 0;
    int use_esi = 0;
    int use_ext = 0;
    int use_timestamp = 0;
    uint32_t arb_id = 0x100;
    int payload_len = 8;
    int rate_us = 0;
    int rt_priority = 80;

    static struct option long_opts[] = {
        {"iface",     required_argument, NULL, 'n'},
        {"fd",        no_argument,       NULL, 'f'},
        {"brs",       no_argument,       NULL, 'b'},
        {"esi",       no_argument,       NULL, 's'},
        {"ext",       no_argument,       NULL, 'e'},
        {"id",        required_argument, NULL, 'i'},
        {"len",       required_argument, NULL, 'l'},
        {"rate",      required_argument, NULL, 'r'},
        {"timestamp", no_argument,       NULL, 't'},
        {"priority",  required_argument, NULL, 'P'},
        {"help",      no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "n:fbsei:l:r:tP:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'n': iface = optarg; break;
        case 'f': use_fd = 1; break;
        case 'b': use_brs = 1; break;
        case 's': use_esi = 1; break;
        case 'e': use_ext = 1; break;
        case 'i': arb_id = (uint32_t)strtoul(optarg, NULL, 16); break;
        case 'l': payload_len = atoi(optarg); break;
        case 'r': rate_us = atoi(optarg); break;
        case 't': use_timestamp = 1; break;
        case 'P': rt_priority = atoi(optarg); break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    if (!iface) {
        fprintf(stderr, "Error: --iface is required\n");
        usage(argv[0]);
        return 1;
    }

    /* Clamp payload length */
    int max_len = use_fd ? 64 : 8;
    if (use_timestamp && payload_len < CANTS_SIZE)
        payload_len = CANTS_SIZE;
    if (payload_len < 0) payload_len = 0;
    if (payload_len > max_len) payload_len = max_len;

    int actual_len;
    if (use_fd) {
        int dlc = len_to_dlc(payload_len);
        actual_len = dlc_to_len[dlc];
    } else {
        actual_len = payload_len;
    }

    /* Build the can_id once (flags + arb_id) */
    canid_t can_id = arb_id;
    if (use_ext)
        can_id |= CAN_EFF_FLAG;

    fprintf(stderr, "Interface: %s\n", iface);
    fprintf(stderr, "Mode:      %s %s\n",
            use_fd ? "CAN FD" : "CAN",
            use_ext ? "(29-bit)" : "(11-bit)");
    fprintf(stderr, "ID:        0x%X\n", arb_id);
    fprintf(stderr, "Length:    %d bytes\n", actual_len);
    fprintf(stderr, "Rate:      %s\n", rate_us > 0 ? "" : "full speed (no delay)");
    if (rate_us > 0) fprintf(stderr, "           %d us/frame\n", rate_us);
    if (use_timestamp) fprintf(stderr, "Timestamp: enabled\n");

    /* Lock memory to prevent page faults causing latency spikes */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0)
        fprintf(stderr, "Warning: mlockall failed: %s\n", strerror(errno));

    /* Apply SCHED_FIFO if priority > 0 */
    if (rt_priority > 0) {
        struct sched_param sp = { .sched_priority = rt_priority };
        if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0)
            fprintf(stderr, "Warning: SCHED_FIFO failed (need root?): %s\n", strerror(errno));
        else
            fprintf(stderr, "RT:        SCHED_FIFO priority %d\n", rt_priority);
    }

    g_sock = can_open(iface, use_fd);
    if (g_sock < 0)
        return 1;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    fprintf(stderr, "Sending frames... (Ctrl+C to stop)\n");

    uint64_t frame_count = 0;
    uint64_t interval_frames = 0;
    struct timespec ts_last, ts_now, ts_next;
    clock_gettime(CLOCK_MONOTONIC, &ts_last);
    ts_next = ts_last;

    /* Build frame once — payload is static */
    struct canfd_frame fd_frame;
    struct can_frame   cl_frame;
    size_t frame_size;
    void  *frame_ptr;

    if (use_fd) {
        memset(&fd_frame, 0, sizeof(fd_frame));
        fd_frame.can_id = can_id;
        fd_frame.len    = (uint8_t)actual_len;
        fd_frame.flags  = 0;
        if (use_brs) fd_frame.flags |= CANFD_BRS;
        if (use_esi) fd_frame.flags |= CANFD_ESI;
        for (int i = 0; i < actual_len; i++)
            fd_frame.data[i] = (uint8_t)i;
        frame_ptr  = &fd_frame;
        frame_size = CANFD_MTU;
    } else {
        memset(&cl_frame, 0, sizeof(cl_frame));
        cl_frame.can_id  = can_id;
        cl_frame.can_dlc = (uint8_t)actual_len;
        for (int i = 0; i < actual_len; i++)
            cl_frame.data[i] = (uint8_t)i;
        frame_ptr  = &cl_frame;
        frame_size = sizeof(cl_frame);
    }

    while (g_running) {
        if (use_timestamp)
            cants_encode((uint8_t *)frame_ptr + (use_fd ? offsetof(struct canfd_frame, data) : offsetof(struct can_frame, data)));

        ssize_t nbytes = write(g_sock, frame_ptr, frame_size);
        if (nbytes < 0) {
            if (errno == EINTR) { g_running = 0; break; }
            perror("write");
            break;
        }
        if (nbytes != (ssize_t)frame_size) {
            fprintf(stderr, "Incomplete write: %zd of %zu bytes\n", nbytes, frame_size);
        }

        frame_count++;
        interval_frames++;

        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double elapsed = (ts_now.tv_sec - ts_last.tv_sec)
                       + (ts_now.tv_nsec - ts_last.tv_nsec) / 1e9;
        if (elapsed >= 1.0) {
            double fps = interval_frames / elapsed;
            double bps = interval_frames * actual_len / elapsed;
            fprintf(stderr, "\r[%lu total]  %.1f frame/s  |  %.1f bytes/s",
                    (unsigned long)frame_count, fps, bps);
            if (bps > 1024)
                fprintf(stderr, "  (%.2f KB/s)", bps / 1024.0);
            fprintf(stderr, "          ");
            interval_frames = 0;
            ts_last = ts_now;
        }

        if (rate_us > 0) {
            ts_next.tv_nsec += rate_us * 1000;
            if (ts_next.tv_nsec >= 1000000000L) {
                ts_next.tv_sec++;
                ts_next.tv_nsec -= 1000000000L;
            }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts_next, NULL);
        }
    }

    fprintf(stderr, "\nShutting down... (sent %lu frames)\n",
            (unsigned long)frame_count);

    close(g_sock);
    return 0;
}

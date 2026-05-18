/*
 * net_reader.c - Read CAN/CANFD frames from a SocketCAN interface.
 *
 * Uses AF_CAN raw socket directly (no slcand, no ASCII protocol).
 *
 * Usage:
 *   ./net_reader --iface can0 [--fd] [--verbose]
 *
 * Compile:
 *   gcc -O2 -Wall -o net_reader net_reader.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
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

    if (enable_fd) {
        int val = 1;
        if (setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &val, sizeof(val)) < 0) {
            perror("setsockopt CAN_RAW_FD_FRAMES");
            close(sock);
            return -1;
        }
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX");
        close(sock);
        return -1;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
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
        "  --fd           Enable CAN FD frames (default: classic CAN)\n"
        "  --verbose      Print each received frame\n"
        "  --priority <n> SCHED_FIFO RT priority 1-99 (default: 80, 0 = disabled)\n"
        "  -h, --help     Show this help\n",
        prog);
}

int main(int argc, char *argv[])
{
    const char *iface = NULL;
    int enable_fd = 0;
    int verbose = 0;
    int use_timestamp = 0;
    int rt_priority = 80;

    static struct option long_opts[] = {
        {"iface",     required_argument, NULL, 'n'},
        {"fd",        no_argument,       NULL, 'f'},
        {"verbose",   no_argument,       NULL, 'v'},
        {"timestamp", no_argument,       NULL, 't'},
        {"priority",  required_argument, NULL, 'P'},
        {"help",      no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "n:fvtP:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'n': iface = optarg; break;
        case 'f': enable_fd = 1; break;
        case 'v': verbose = 1; break;
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

    fprintf(stderr, "Interface: %s\n", iface);
    fprintf(stderr, "Mode:      %s\n", enable_fd ? "CAN FD" : "CAN");
    if (use_timestamp) fprintf(stderr, "Timestamp: enabled\n");

    g_sock = can_open(iface, enable_fd);
    if (g_sock < 0)
        return 1;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    fprintf(stderr, "Listening... (Ctrl+C to stop)\n");

    uint64_t total_packets = 0;
    uint64_t total_bytes = 0;
    uint64_t interval_packets = 0;
    uint64_t interval_bytes = 0;

    CantsStats lat_stats = {0};
    if (use_timestamp && cants_stats_init(&lat_stats) < 0) {
        fprintf(stderr, "Failed to allocate latency buffer\n");
        return 1;
    }

    struct timespec ts_last, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_last);

    while (g_running) {
        struct canfd_frame frame;
        ssize_t n = read(g_sock, &frame, CANFD_MTU);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            break;
        }
        if (n == 0) continue;

        int is_fd_frame = (n == CANFD_MTU);
        int len = enable_fd ? frame.len
                            : ((struct can_frame *)&frame)->can_dlc;
        if (len > (is_fd_frame ? 64 : 8))
            len = is_fd_frame ? 64 : 8;

        if (use_timestamp && len >= CANTS_SIZE)
            cants_stats_add(&lat_stats, cants_decode(frame.data));

        if (verbose) {
            int is_ext = !!(frame.can_id & CAN_EFF_FLAG);
            uint32_t id = frame.can_id & (is_ext ? CAN_EFF_MASK : CAN_SFF_MASK);
            printf("%s %s ID=0x%0*X len=%d ",
                   is_fd_frame ? "FD" : "CL",
                   is_ext ? "EXT" : "STD",
                   is_ext ? 8 : 3, id, len);
            for (int i = 0; i < len && i < 16; i++)
                printf("%02X ", frame.data[i]);
            if (len > 16) printf("...");
            printf("\n");
        }

        interval_packets++;
        interval_bytes += len;
        total_packets++;
        total_bytes += len;

        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double elapsed = (ts_now.tv_sec - ts_last.tv_sec)
                       + (ts_now.tv_nsec - ts_last.tv_nsec) / 1e9;

        if (elapsed >= 1.0) {
            double pps = interval_packets / elapsed;
            double bps = interval_bytes / elapsed;

            printf("[%lu total]  %.1f pkt/s  |  %.1f bytes/s",
                   (unsigned long)total_packets, pps, bps);
            if (bps > 1024)
                printf("  (%.2f KB/s)", bps / 1024.0);

            if (use_timestamp)
                cants_stats_print(&lat_stats);

            printf("\n");
            fflush(stdout);

            interval_packets = 0;
            interval_bytes = 0;
            ts_last = ts_now;
        }
    }

    fprintf(stderr, "\nShutting down... (received %lu packets, %lu bytes total)\n",
            (unsigned long)total_packets, (unsigned long)total_bytes);

    close(g_sock);
    return 0;
}

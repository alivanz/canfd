/*
 * sange_reader.c - Receive CAN frames on one channel of SanGe PCIe CAN-FD card.
 *
 * Opens /dev/ch36xpci0, inits the specified channel, then polls for incoming
 * TJSG packets from the STM32. A companion sange_writer can run simultaneously
 * on another channel; flock() prevents FIFO interleaving between them.
 *
 * Known device→host commands:
 *   cmd=0x01 → InitCan ACK (empty payload)
 *   cmd=0x07 → TX ACK:    CanIndex[4] + TxSeq[4]
 *   cmd=0x05 → RX FRAME:  (payload format TBD — printed raw + guessed)
 *   cmd=0x??  anything else printed as hex for discovery
 *
 * Usage:
 *   ./sange_reader --channel 1 [--verbose]
 *
 * Compile:
 *   gcc -O2 -Wall -o sange_reader sange_reader.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <getopt.h>
#include "sange.h"
#include "cants.h"

static volatile sig_atomic_t g_running = 1;
static void sig_handler(int s) { (void)s; g_running = 0; }

/*
 * Decode and print one received TJSG packet.
 * Returns 1 if it looks like a received CAN frame, 0 otherwise.
 */
static int decode_packet(const uint8_t *pkt, int len, int verbose,
                         CantsStats *lat)
{
    uint8_t cmd         = pkt[6];
    int     payload_len = len - 9;
    const uint8_t *pl   = pkt + 8;

    switch (cmd) {
    case 0x01:
        fprintf(stderr, "[rx] InitCan ACK (len=%d)\n", len);
        return 0;

    case 0x07:
        /* TX done: CanIndex[4] + TxSeq[4] */
        if (verbose && payload_len >= 8) {
            uint32_t ch, seq;
            memcpy(&ch,  pl,     4);
            memcpy(&seq, pl + 4, 4);
            fprintf(stderr, "[rx] TX ACK  ch=%u seq=%u\n", ch, seq);
        }
        return 0;

    case 0x06:
        /* Received CAN frame from device.
         * Layout: CanIndex[4] + unknown[8] + VCI_CAN_OBJ[72] = 84 bytes payload
         * VCI_CAN_OBJ starts at pkt[20] = pl[12] */
        if (payload_len >= 84) {
            uint32_t ch;
            VCI_CAN_OBJ obj;
            memcpy(&ch,  pl,      4);
            memcpy(&obj, pl + 12, sizeof(obj));
            int dl = obj.DataLen > 64 ? 64 : (int)obj.DataLen;

            if (lat && dl >= CANTS_SIZE)
                cants_stats_add(lat, cants_decode(obj.Data));

            printf("[ch%u] ID=0x%0*X %s len=%2d  data:",
                   ch,
                   obj.ExternFlag ? 8 : 3,
                   obj.ID & (obj.ExternFlag ? 0x1FFFFFFF : 0x7FF),
                   obj.FDFlag ? "FD" : "  ",
                   dl);
            for (int i = 0; i < dl && i < 16; i++)
                printf(" %02X", obj.Data[i]);
            if (dl > 16) printf(" ...");
            printf("\n");
            fflush(stdout);
            return 1;
        }
        /* payload too short — fall through to raw print */
        /* fall through */

    default:
        /* Unknown cmd — print everything raw for discovery */
        printf("[rx] cmd=0x%02X len=%d payload=%d bytes:", cmd, len, payload_len);
        for (int i = 0; i < payload_len && i < 32; i++)
            printf(" %02X", pl[i]);
        if (payload_len > 32) printf(" ...");
        printf("\n");
        fflush(stdout);

        /* Also try interpreting trailing 72 bytes as VCI_CAN_OBJ */
        if (payload_len >= 72) {
            VCI_CAN_OBJ obj;
            memcpy(&obj, pl + payload_len - 72, sizeof(obj));
            if (obj.DataLen <= 64) {
                int dl = (int)obj.DataLen;
                printf("       → as CAN_OBJ tail: ID=0x%X EXT=%d FD=%d len=%d  data:",
                       obj.ID, obj.ExternFlag, obj.FDFlag, dl);
                for (int i = 0; i < dl && i < 16; i++)
                    printf(" %02X", obj.Data[i]);
                printf("\n");
                fflush(stdout);
            }
        }
        return 0;
    }
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --dev <path>     ch36x device (default: /dev/ch36xpci0)\n"
        "  --channel <n>    CAN channel to receive on (default: 1)\n"
        "  --timing0 <hex>  Nominal bitrate byte (default: 0x60)\n"
        "  --timing1 <hex>  Data bitrate byte for FD (default: 0x60)\n"
        "  --fd             Enable CAN FD frames\n"
        "  --timestamp      Decode latency timestamps (needs sange_writer --timestamp)\n"
        "  --verbose        Print TX ACKs and raw bytes too\n"
        "  -h, --help       Show this help\n",
        prog);
}

int main(int argc, char *argv[])
{
    const char *dev  = "/dev/ch36xpci0";
    uint32_t channel = 1;
    uint8_t timing0  = 0x60;
    uint8_t timing1  = 0x60;
    int  use_fd      = 0;
    int  use_ts      = 0;
    int  verbose     = 0;

    static struct option long_opts[] = {
        {"dev",       required_argument, NULL, 'd'},
        {"channel",   required_argument, NULL, 'c'},
        {"timing0",   required_argument, NULL, '0'},
        {"timing1",   required_argument, NULL, '1'},
        {"fd",        no_argument,       NULL, 'f'},
        {"timestamp", no_argument,       NULL, 't'},
        {"verbose",   no_argument,       NULL, 'v'},
        {"help",      no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "d:c:0:1:ftvh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'd': dev     = optarg; break;
        case 'c': channel = (uint32_t)atoi(optarg); break;
        case '0': timing0 = (uint8_t)strtoul(optarg, NULL, 16); break;
        case '1': timing1 = (uint8_t)strtoul(optarg, NULL, 16); break;
        case 'f': use_fd  = 1; break;
        case 't': use_ts  = 1; break;
        case 'v': verbose = 1; break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    fprintf(stderr, "Opened %s\n", dev);

    /* Set IO speed */
    flock(fd, LOCK_EX);
    io_write(fd, 0xFA, 0);
    flock(fd, LOCK_UN);

    /* Init CAN channel */
    VCI_INIT_CONFIG cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.FDFlag  = use_fd ? 1 : 0;
    cfg.Timing0 = timing0;
    cfg.Timing1 = use_fd ? timing1 : 0;
    cfg.Mode    = 0;  /* normal receive mode */

    fprintf(stderr, "VCI_SetValue ch=%u (accept-all filter)\n", channel);
    sange_set_filter(fd, channel);

    fprintf(stderr, "VCI_InitCan ch=%u timing0=0x%02X FD=%d\n",
            channel, timing0, use_fd);
    sange_init_can(fd, channel, &cfg);

    /* Wait for InitCan ACK */
    usleep(200000);
    {
        uint8_t raw[256];
        int rd = sange_poll_rx(fd, raw, sizeof(raw));
        if (rd > 0) {
            fprintf(stderr, "InitCan response (%d bytes):", rd);
            for (int i = 0; i < rd && i < 32; i++) fprintf(stderr, " %02X", raw[i]);
            fprintf(stderr, "\n");
        }
    }

    fprintf(stderr, "Listening on ch%u (Ctrl+C to stop)\n", channel);

    CantsStats lat_stats = {0};
    if (use_ts && cants_stats_init(&lat_stats) < 0) {
        fprintf(stderr, "Failed to allocate latency buffer\n");
        return 1;
    }

    uint64_t total_packets  = 0;
    uint64_t interval_pkts  = 0;
    SangeRing ring = {0};

    struct timespec ts_last, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_last);

    while (g_running) {
        uint8_t raw[2048];
        int rd = sange_poll_rx(fd, raw, sizeof(raw));
        if (rd > 0) {
            if (verbose) {
                fprintf(stderr, "[fifo] %d bytes:", rd);
                for (int i = 0; i < rd && i < 32; i++)
                    fprintf(stderr, " %02X", raw[i]);
                if (rd > 32) fprintf(stderr, " ...");
                fprintf(stderr, "\n");
            }
            sange_ring_push(&ring, raw, rd);

            uint8_t pktbuf[512];
            int r;
            while ((r = sange_parse_one(&ring, pktbuf, sizeof(pktbuf))) != 0) {
                if (r > 0) {
                    int is_frame = decode_packet(pktbuf, r, verbose,
                                                 use_ts ? &lat_stats : NULL);
                    if (is_frame) {
                        total_packets++;
                        interval_pkts++;
                    }
                }
            }
        } else {
            /* Nothing available — short sleep to avoid busy loop */
            usleep(500);
        }

        /* Stats every second */
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double elapsed = (ts_now.tv_sec - ts_last.tv_sec)
                       + (ts_now.tv_nsec - ts_last.tv_nsec) / 1e9;
        if (elapsed >= 1.0) {
            double pps = interval_pkts / elapsed;
            fprintf(stderr, "\r[%llu total]  %.1f pkt/s",
                    (unsigned long long)total_packets, pps);
            if (use_ts) cants_stats_print(&lat_stats);
            fprintf(stderr, "          ");
            interval_pkts = 0;
            ts_last = ts_now;
        }
    }

    fprintf(stderr, "\nShutting down (received %llu frames)\n",
            (unsigned long long)total_packets);
    close(fd);
    return 0;
}

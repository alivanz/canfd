/*
 * sange_writer.c - Send CAN frames on one channel of SanGe PCIe CAN-FD card.
 *
 * Opens /dev/ch36xpci0, inits the specified channel, then sends frames
 * continuously. A companion sange_reader can run simultaneously on the
 * other channel; flock() prevents FIFO interleaving between them.
 *
 * Usage:
 *   ./sange_writer --channel 0 [--id 0x100] [--len 8] [--rate 0] [--fd]
 *
 * Compile:
 *   gcc -O2 -Wall -o sange_writer sange_writer.c
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

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --dev <path>     ch36x device (default: /dev/ch36xpci0)\n"
        "  --channel <n>    CAN channel to transmit on (default: 0)\n"
        "  --id <hex>       CAN frame arbitration ID (default: 0x100)\n"
        "  --len <n>        Payload length in bytes (default: 8)\n"
        "  --timing0 <hex>  Nominal bitrate byte (default: 0x60)\n"
        "  --timing1 <hex>  Data bitrate byte for FD (default: 0x60)\n"
        "  --fd             Enable CAN FD frames\n"
        "  --ext            Use extended 29-bit ID\n"
        "  --rate <us>      Interval between frames in microseconds\n"
        "                   (default: 0 = as fast as possible)\n"
        "  --timestamp      Embed send timestamp for latency measurement\n"
        "  -h, --help       Show this help\n",
        prog);
}

int main(int argc, char *argv[])
{
    const char *dev  = "/dev/ch36xpci0";
    uint32_t channel = 0;
    uint32_t arb_id  = 0x100;
    int  data_len    = 8;
    uint8_t timing0  = 0x60;
    uint8_t timing1  = 0x60;
    int  use_fd      = 0;
    int  use_ext     = 0;
    int  rate_us     = 0;
    int  use_ts      = 0;

    static struct option long_opts[] = {
        {"dev",       required_argument, NULL, 'd'},
        {"channel",   required_argument, NULL, 'c'},
        {"id",        required_argument, NULL, 'i'},
        {"len",       required_argument, NULL, 'l'},
        {"timing0",   required_argument, NULL, '0'},
        {"timing1",   required_argument, NULL, '1'},
        {"fd",        no_argument,       NULL, 'f'},
        {"ext",       no_argument,       NULL, 'e'},
        {"rate",      required_argument, NULL, 'r'},
        {"timestamp", no_argument,       NULL, 't'},
        {"help",      no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "d:c:i:l:0:1:fer:th", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'd': dev      = optarg; break;
        case 'c': channel  = (uint32_t)atoi(optarg); break;
        case 'i': arb_id   = (uint32_t)strtoul(optarg, NULL, 16); break;
        case 'l': data_len = atoi(optarg); break;
        case '0': timing0  = (uint8_t)strtoul(optarg, NULL, 16); break;
        case '1': timing1  = (uint8_t)strtoul(optarg, NULL, 16); break;
        case 'f': use_fd   = 1; break;
        case 'e': use_ext  = 1; break;
        case 'r': rate_us  = atoi(optarg); break;
        case 't': use_ts   = 1; break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    int max_len = use_fd ? 64 : 8;
    if (use_ts && data_len < CANTS_SIZE) data_len = CANTS_SIZE;
    if (data_len < 0) data_len = 0;
    if (data_len > max_len) data_len = max_len;

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
    cfg.Mode    = 0;  /* normal mode */

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

    fprintf(stderr, "Sending on ch%u  ID=0x%X  len=%d  %s\n",
            channel, arb_id, data_len,
            rate_us > 0 ? "" : "(full speed)");
    if (rate_us > 0)
        fprintf(stderr, "Rate: %d us/frame\n", rate_us);
    if (use_ts)
        fprintf(stderr, "Timestamp: enabled\n");

    uint64_t total_frames = 0;
    uint64_t interval_frames = 0;
    uint8_t  counter = 0;
    uint32_t seq = 0;

    struct timespec ts_start, ts_last, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    ts_last = ts_start;

    while (g_running) {
        VCI_CAN_OBJ obj;
        memset(&obj, 0, sizeof(obj));
        obj.ID         = arb_id;
        obj.ExternFlag = use_ext ? 1 : 0;
        obj.FDFlag     = use_fd  ? 1 : 0;
        obj.DataLen    = (uint8_t)data_len;

        if (use_ts) {
            cants_encode(obj.Data);
            for (int i = CANTS_SIZE; i < data_len; i++)
                obj.Data[i] = (uint8_t)(counter + i);
        } else {
            for (int i = 0; i < data_len; i++)
                obj.Data[i] = (uint8_t)(counter + i);
        }
        counter++;

        if (sange_transmit(fd, channel, &obj, seq++) > 0) {
            total_frames++;
            interval_frames++;
        }

        if (rate_us > 0) usleep(rate_us);

        /* Stats every second */
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double elapsed = (ts_now.tv_sec - ts_last.tv_sec)
                       + (ts_now.tv_nsec - ts_last.tv_nsec) / 1e9;
        if (elapsed >= 1.0) {
            double fps = interval_frames / elapsed;
            fprintf(stderr, "\r[%llu total]  %.1f frame/s          ",
                    (unsigned long long)total_frames, fps);
            interval_frames = 0;
            ts_last = ts_now;
        }
    }

    fprintf(stderr, "\nShutting down (sent %llu frames)\n",
            (unsigned long long)total_frames);
    close(fd);
    return 0;
}

/*
 * slcan_writer.c - Write SLCAN frames directly to a raw TTY serial port.
 *
 * SLCAN ASCII protocol (no slcand):
 *   CAN   standard 11-bit:  tIIILDD..\r
 *   CAN   extended 29-bit:  TIIIIIIIILDD..\r
 *   CANFD standard 11-bit:  dIIILDD..\r
 *   CANFD extended 29-bit:  DIIIIIIIILDD..\r
 *
 * DLC-to-byte-count for CAN FD (DLC > 8):
 *   9→12  A→16  B→20  C→24  D→32  E→48  F→64
 *
 * Usage:
 *   ./slcan_writer --port /dev/ttyUSB0 [--fd] [--id 0x100] [--len 8] [--baud 115200] [--bitrate 500k] [--dbitrate 5M] [--ext] [--rate 0]
 *
 * Compile:
 *   gcc -O2 -Wall -o slcan_writer slcan_writer.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <termios.h>
#include <sys/ioctl.h>

static volatile sig_atomic_t g_running = 1;
static int g_fd = -1;

/* ---- Internal write buffer ---- */
#define SLCAN_BUF_SIZE 4096

typedef struct {
    int      fd;
    char     buf[SLCAN_BUF_SIZE];
    int      used;
} SlcanBuf;

static void slcanbuf_init(SlcanBuf *b, int fd)
{
    b->fd   = fd;
    b->used = 0;
}

static void slcanbuf_flush(SlcanBuf *b)
{
    if (b->used == 0)
        return;
    write(b->fd, b->buf, b->used);
    b->used = 0;
}

static void slcanbuf_write(SlcanBuf *b, const char *data, int len)
{
    if (b->used + len > SLCAN_BUF_SIZE)
        slcanbuf_flush(b);
    memcpy(b->buf + b->used, data, len);
    b->used += len;
}

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
    return 15; /* 64 bytes max */
}

static speed_t baud_to_speed(int baud)
{
    switch (baud) {
    case 9600:    return B9600;
    case 19200:   return B19200;
    case 38400:   return B38400;
    case 57600:   return B57600;
    case 115200:  return B115200;
    case 230400:  return B230400;
#ifdef B460800
    case 460800:  return B460800;
#endif
#ifdef B500000
    case 500000:  return B500000;
#endif
#ifdef B921600
    case 921600:  return B921600;
#endif
#ifdef B1000000
    case 1000000: return B1000000;
#endif
#ifdef B2000000
    case 2000000: return B2000000;
#endif
#ifdef B3000000
    case 3000000: return B3000000;
#endif
    default:
        fprintf(stderr, "Unsupported baud rate %d, using 115200\n", baud);
        return B115200;
    }
}

static int parse_bitrate(const char *s)
{
    char *end;
    double v = strtod(s, &end);
    if (end == s) return -1;
    if (*end == 'k' || *end == 'K') v *= 1000;
    else if (*end == 'M' || *end == 'm') v *= 1000000;
    return (int)v;
}

/* Standard SLCAN S0-S8 nominal bitrate commands */
static const char *bitrate_to_s_cmd(int bitrate)
{
    switch (bitrate) {
    case 10000:   return "S0\r";
    case 20000:   return "S1\r";
    case 50000:   return "S2\r";
    case 100000:  return "S3\r";
    case 125000:  return "S4\r";
    case 250000:  return "S5\r";
    case 500000:  return "S6\r";
    case 800000:  return "S7\r";
    case 1000000: return "S8\r";
    default:      return NULL;
    }
}

/* CAN FD data bitrate Y commands (CANable 2.0 / slcan firmware) */
static const char *dbitrate_to_y_cmd(int bitrate)
{
    switch (bitrate) {
    case 1000000: return "Y0\r";
    case 2000000: return "Y1\r";
    case 4000000: return "Y2\r";
    case 5000000: return "Y3\r";
    case 8000000: return "Y4\r";
    default:      return NULL;
    }
}

static int tty_open(const char *port, int baud)
{
    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    /* Clear non-blocking after open */
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tios;
    memset(&tios, 0, sizeof(tios));
    cfmakeraw(&tios);

    tios.c_cflag |= (CLOCAL | CREAD);
    tios.c_cflag &= ~CRTSCTS;
    tios.c_iflag &= ~(IXON | IXOFF | IXANY);

    speed_t spd = baud_to_speed(baud);
    cfsetispeed(&tios, spd);
    cfsetospeed(&tios, spd);

    /* Blocking read with 100ms timeout */
    tios.c_cc[VMIN] = 0;
    tios.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSAFLUSH, &tios) < 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

static void slcan_send_cmd(int fd, const char *cmd)
{
    write(fd, cmd, strlen(cmd));
    tcdrain(fd);
    usleep(50000); /* 50ms settle */
}

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s --port <device> [options]\n"
        "\n"
        "Options:\n"
        "  --port <dev>      Serial port (e.g. /dev/ttyUSB0)  [required]\n"
        "  --fd              Use CAN FD frames (default: classic CAN)\n"
        "  --ext             Use extended 29-bit ID (default: standard 11-bit)\n"
        "  --id <hex>        Arbitration ID in hex (default: 100)\n"
        "  --len <n>         Payload length in bytes (default: 8)\n"
        "  --baud <rate>     Serial baud rate (default: 115200)\n"
        "  --bitrate <rate>  CAN nominal bitrate: 10k/20k/50k/100k/125k/250k/500k/800k/1M (default: 1M)\n"
        "  --dbitrate <rate> CAN FD data bitrate: 1M/2M/4M/5M/8M (default: none)\n"
        "  --rate <us>       Interval between frames in microseconds (default: 0 = full speed)\n"
        "  -h, --help        Show this help\n",
        prog);
}

int main(int argc, char *argv[])
{
    const char *port = NULL;
    int use_fd = 0;
    int use_ext = 0;
    uint32_t arb_id = 0x100;
    int payload_len = 8;
    int baud = 115200;
    int bitrate = 1000000;
    int dbitrate = -1;
    int rate_us = 0;

    static struct option long_opts[] = {
        {"port",     required_argument, NULL, 'p'},
        {"fd",       no_argument,       NULL, 'f'},
        {"ext",      no_argument,       NULL, 'e'},
        {"id",       required_argument, NULL, 'i'},
        {"len",      required_argument, NULL, 'l'},
        {"baud",     required_argument, NULL, 'b'},
        {"bitrate",  required_argument, NULL, 'B'},
        {"dbitrate", required_argument, NULL, 'D'},
        {"rate",     required_argument, NULL, 'r'},
        {"help",     no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:fei:l:b:B:D:r:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'p': port = optarg; break;
        case 'f': use_fd = 1; break;
        case 'e': use_ext = 1; break;
        case 'i': arb_id = (uint32_t)strtoul(optarg, NULL, 16); break;
        case 'l': payload_len = atoi(optarg); break;
        case 'b': baud = atoi(optarg); break;
        case 'B': bitrate = parse_bitrate(optarg); break;
        case 'D': dbitrate = parse_bitrate(optarg); break;
        case 'r': rate_us = atoi(optarg); break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    if (!port) {
        fprintf(stderr, "Error: --port is required\n");
        usage(argv[0]);
        return 1;
    }

    /* Clamp payload length */
    int max_len = use_fd ? 64 : 8;
    if (payload_len < 0) payload_len = 0;
    if (payload_len > max_len) payload_len = max_len;

    /* Compute DLC */
    int dlc;
    int actual_len;
    if (use_fd) {
        dlc = len_to_dlc(payload_len);
        actual_len = dlc_to_len[dlc];
    } else {
        if (payload_len > 8) payload_len = 8;
        dlc = payload_len;
        actual_len = payload_len;
    }

    fprintf(stderr, "Port:    %s @ %d baud\n", port, baud);
    fprintf(stderr, "Mode:    %s %s\n",
            use_fd ? "CAN FD" : "CAN",
            use_ext ? "(29-bit)" : "(11-bit)");
    fprintf(stderr, "ID:      0x%X\n", arb_id);
    fprintf(stderr, "DLC:     %d (0x%X) → %d bytes\n", dlc, dlc, actual_len);
    fprintf(stderr, "Bitrate: %d\n", bitrate);
    if (dbitrate > 0)
        fprintf(stderr, "DbitRate:%d\n", dbitrate);
    fprintf(stderr, "Rate:    %s\n", rate_us > 0 ? "" : "full speed (no delay)");
    if (rate_us > 0) fprintf(stderr, "         %d us/frame\n", rate_us);

    /* Open serial port */
    g_fd = tty_open(port, baud);
    if (g_fd < 0)
        return 1;

    /* Install signal handler */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Close any previous session, configure bitrate, open channel */
    slcan_send_cmd(g_fd, "C\r");
    const char *s_cmd = bitrate_to_s_cmd(bitrate);
    if (s_cmd) {
        slcan_send_cmd(g_fd, s_cmd);
    } else {
        fprintf(stderr, "Warning: unsupported bitrate %d, skipping S command\n", bitrate);
    }
    if (dbitrate > 0) {
        const char *y_cmd = dbitrate_to_y_cmd(dbitrate);
        if (y_cmd)
            slcan_send_cmd(g_fd, y_cmd);
        else
            fprintf(stderr, "Warning: unsupported dbitrate %d, skipping Y command\n", dbitrate);
    }
    slcan_send_cmd(g_fd, "O\r");

    fprintf(stderr, "Sending frames... (Ctrl+C to stop)\n");

    SlcanBuf sbuf;
    slcanbuf_init(&sbuf, g_fd);

    uint64_t frame_count = 0;
    uint64_t interval_frames = 0;
    uint8_t counter = 0;
    struct timespec ts_start, ts_now, ts_last;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    ts_last = ts_start;

    while (g_running) {
        /*
         * Build SLCAN ASCII frame:
         *   cmd + id + dlc_char + hex_data + \r
         * Max size: 1 + 8 + 1 + 128 + 1 + 1 = 140
         */
        char buf[256];
        int pos = 0;

        /* Command character */
        if (use_fd) {
            buf[pos++] = use_ext ? 'D' : 'd';
        } else {
            buf[pos++] = use_ext ? 'T' : 't';
        }

        /* Arbitration ID */
        if (use_ext) {
            pos += sprintf(&buf[pos], "%08X", arb_id & 0x1FFFFFFF);
        } else {
            pos += sprintf(&buf[pos], "%03X", arb_id & 0x7FF);
        }

        /* DLC as single hex char */
        buf[pos++] = (dlc < 10) ? ('0' + dlc) : ('A' + dlc - 10);

        /* Payload: fill with incrementing counter */
        for (int i = 0; i < actual_len; i++) {
            pos += sprintf(&buf[pos], "%02X", (uint8_t)(counter + i));
        }

        buf[pos++] = '\r';
        buf[pos] = '\0';

        slcanbuf_write(&sbuf, buf, pos);

        frame_count++;
        interval_frames++;
        counter++;

        /* Stats every second */
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

        /* Rate limiting */
        if (rate_us > 0)
            usleep(rate_us);
    }

    slcanbuf_flush(&sbuf);

    fprintf(stderr, "\nShutting down... (sent %lu frames)\n",
            (unsigned long)frame_count);

    /* Close SLCAN channel */
    slcan_send_cmd(g_fd, "C\r");
    close(g_fd);

    return 0;
}

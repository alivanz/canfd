/*
 * slcan_reader.c - Read SLCAN frames directly from a raw TTY serial port.
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
 *   ./slcan_reader --port /dev/ttyUSB0 [--fd] [--baud 115200] [--bitrate 500k] [--dbitrate 5M] [--verbose]
 *
 * Compile:
 *   gcc -O2 -Wall -o slcan_reader slcan_reader.c
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
#include <ctype.h>
#include <getopt.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>

static volatile sig_atomic_t g_running = 1;
static int g_fd = -1;

/* DLC value (0-F) → actual byte count for CAN FD */
static const int dlc_to_len[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8,   /* 0-8 */
    12, 16, 20, 24, 32, 48, 64    /* 9-F */
};

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

    /* Non-blocking read: return immediately with available data */
    tios.c_cc[VMIN] = 0;
    tios.c_cc[VTIME] = 1; /* 100ms timeout */

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

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/*
 * Parse one SLCAN frame line (without trailing \r).
 * Returns the payload byte count, or -1 if not a data frame.
 */
static int parse_frame(const char *line, int len, int verbose)
{
    if (len < 2) return -1;

    char cmd = line[0];
    int is_fd = 0;
    int is_ext = 0;
    int id_chars;

    switch (cmd) {
    case 't': is_fd = 0; is_ext = 0; id_chars = 3; break;
    case 'T': is_fd = 0; is_ext = 1; id_chars = 8; break;
    case 'd': is_fd = 1; is_ext = 0; id_chars = 3; break;
    case 'D': is_fd = 1; is_ext = 1; id_chars = 8; break;
    case 'r': /* RTR standard */
    case 'R': /* RTR extended */
        return 0; /* RTR has no payload */
    default:
        return -1; /* Not a data frame (could be response like \x07 or 'z') */
    }

    /* If user selected CAN but got FD, or vice versa, still count it */

    int pos = 1;

    /* Parse arbitration ID */
    if (pos + id_chars > len) return -1;
    uint32_t arb_id = 0;
    for (int i = 0; i < id_chars; i++) {
        int v = hex_val(line[pos++]);
        if (v < 0) return -1;
        arb_id = (arb_id << 4) | v;
    }

    /* Parse DLC */
    if (pos >= len) return -1;
    int dlc = hex_val(line[pos++]);
    if (dlc < 0) return -1;

    int data_bytes;
    if (is_fd) {
        if (dlc < 0 || dlc > 15) return -1;
        data_bytes = dlc_to_len[dlc];
    } else {
        if (dlc > 8) return -1;
        data_bytes = dlc;
    }

    /* Parse data (each byte = 2 hex chars) */
    int hex_data_chars = data_bytes * 2;
    if (pos + hex_data_chars > len) {
        /* Truncated frame — count what we got */
        data_bytes = (len - pos) / 2;
    }

    if (verbose) {
        printf("%s %s ID=0x%0*X DLC=%d (%d bytes) ",
               is_fd ? "FD" : "  ",
               is_ext ? "EXT" : "STD",
               id_chars, arb_id,
               dlc, data_bytes);

        /* Print payload hex */
        int available = (len - pos) / 2;
        if (available > data_bytes) available = data_bytes;
        for (int i = 0; i < available; i++) {
            int hi = hex_val(line[pos + i * 2]);
            int lo = hex_val(line[pos + i * 2 + 1]);
            if (hi >= 0 && lo >= 0)
                printf("%02X ", (hi << 4) | lo);
        }
        printf("\n");
    }

    return data_bytes;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s --port <device> [options]\n"
        "\n"
        "Options:\n"
        "  --port <dev>      Serial port (e.g. /dev/ttyUSB0)  [required]\n"
        "  --fd              Expect CAN FD frames (default: classic CAN)\n"
        "  --baud <rate>     Serial baud rate (default: 115200)\n"
        "  --bitrate <rate>  CAN nominal bitrate: 10k/20k/50k/100k/125k/250k/500k/800k/1M (default: 1M)\n"
        "  --dbitrate <rate> CAN FD data bitrate: 1M/2M/4M/5M/8M (default: none)\n"
        "  --verbose         Print each received frame\n"
        "  -h, --help        Show this help\n",
        prog);
}

int main(int argc, char *argv[])
{
    const char *port = NULL;
    int expect_fd = 0;
    int baud = 115200;
    int bitrate = 1000000;
    int dbitrate = -1;
    int verbose = 0;

    static struct option long_opts[] = {
        {"port",     required_argument, NULL, 'p'},
        {"fd",       no_argument,       NULL, 'f'},
        {"baud",     required_argument, NULL, 'b'},
        {"bitrate",  required_argument, NULL, 'B'},
        {"dbitrate", required_argument, NULL, 'D'},
        {"verbose",  no_argument,       NULL, 'v'},
        {"help",     no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:fb:B:D:vh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'p': port = optarg; break;
        case 'f': expect_fd = 1; break;
        case 'b': baud = atoi(optarg); break;
        case 'B': bitrate = parse_bitrate(optarg); break;
        case 'D': dbitrate = parse_bitrate(optarg); break;
        case 'v': verbose = 1; break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    if (!port) {
        fprintf(stderr, "Error: --port is required\n");
        usage(argv[0]);
        return 1;
    }

    fprintf(stderr, "Port:    %s @ %d baud\n", port, baud);
    fprintf(stderr, "Mode:    %s\n", expect_fd ? "CAN FD" : "CAN");
    fprintf(stderr, "Bitrate: %d\n", bitrate);
    if (dbitrate > 0)
        fprintf(stderr, "DbitRate:%d\n", dbitrate);

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

    fprintf(stderr, "Listening... (Ctrl+C to stop)\n");

    /* Line buffer for assembling SLCAN frames */
    char linebuf[512];
    int linepos = 0;

    /* Stats */
    uint64_t total_packets = 0;
    uint64_t total_bytes = 0;
    uint64_t interval_packets = 0;
    uint64_t interval_bytes = 0;

    struct timespec ts_last, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_last);

    while (g_running) {
        /* Use select() for interruptible reads */
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(g_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000; /* 100ms */

        int ret = select(g_fd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (ret > 0 && FD_ISSET(g_fd, &rfds)) {
            char rxbuf[256];
            ssize_t n = read(g_fd, rxbuf, sizeof(rxbuf));
            if (n < 0) {
                if (errno == EINTR) continue;
                perror("read");
                break;
            }
            if (n == 0) continue;

            /* Assemble lines delimited by \r */
            for (ssize_t i = 0; i < n; i++) {
                char c = rxbuf[i];
                if (c == '\r' || c == '\n') {
                    if (linepos > 0) {
                        linebuf[linepos] = '\0';
                        int payload = parse_frame(linebuf, linepos, verbose);
                        if (payload >= 0) {
                            interval_packets++;
                            interval_bytes += payload;
                            total_packets++;
                            total_bytes += payload;
                        }
                        linepos = 0;
                    }
                } else {
                    if (linepos < (int)sizeof(linebuf) - 1)
                        linebuf[linepos++] = c;
                }
            }
        }

        /* Print stats every second */
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
            printf("\n");
            fflush(stdout);

            interval_packets = 0;
            interval_bytes = 0;
            ts_last = ts_now;
        }
    }

    fprintf(stderr, "\nShutting down... (received %lu packets, %lu bytes total)\n",
            (unsigned long)total_packets, (unsigned long)total_bytes);

    /* Close SLCAN channel */
    slcan_send_cmd(g_fd, "C\r");
    close(g_fd);

    return 0;
}

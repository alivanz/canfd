/*
 * sange_can.c - Linux driver/test tool for SanGe SG-PCIe-CAN(FD)-200T
 *
 * Hardware: WCH CH367 PCIe bridge → CH424Q FIFO → STM32H750 CAN controller
 * Kernel driver: ch36x_linux (creates /dev/ch36xpci0)
 *
 * FIFO protocol (CH424Q via CH367 IO registers):
 *   Reg 0x00 = data byte (read/write)
 *   Reg 0xF1 = mode control:
 *     0x02 = query available bytes in FIFO
 *     0x81 = assert CS for writing to STM32
 *     0x82 = read bytes from STM32
 *     0x83 = deassert CS (end transaction)
 *
 * Wire packet format (TJSG protocol):
 *   [0..3]  = "TJSG" magic (0x54 0x4A 0x53 0x47)
 *   [4]     = total_len >> 8
 *   [5]     = total_len & 0xFF  (= entire packet size including TJSG+checksum)
 *   [6]     = command byte
 *   [7]     = 0x00 (reserved)
 *   [8..n]  = payload
 *   [n+1]   = checksum = sum(bytes[0..n]) & 0xFF
 *
 * Commands (host→device):
 *   0x01 = VCI_InitCan   payload: CanIndex[4] + VCI_INIT_CONFIG[8]  → 21 bytes
 *   0x06 = VCI_Transmit  payload: CanIndex[4]+Count[4]+Seq[4]+OBJ[72] → 93 bytes
 *
 * Device→host responses: same TJSG format, observed:
 *   cmd=0x01, len=9  = ACK for InitCan (empty payload)
 *   cmd=0x??, len=?? = received CAN frame (TBD by testing)
 *
 * Timing0/Timing1 encoding (SanGe SDK Timing[] array):
 *   0xB0..0x10 in steps of 0x10, typical mapping guessed as:
 *   0xB0≈10k, 0xA0≈20k, 0x90≈50k, 0x80≈100k, 0x70≈125k,
 *   0x60≈250k, 0x50≈500k, 0x40≈800k, 0x30≈1M, 0x20≈2M, 0x10≈5M
 *
 * Compile:
 *   gcc -O2 -Wall -o sange_can sange_can.c
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
#include <sys/ioctl.h>

/* ---- CH36x ioctl interface ---- */
#define IOCTL_MAGIC         'P'
#define CH36x_READ_IO_BYTE  _IOR(IOCTL_MAGIC, 0x88, uint16_t)
#define CH36x_WRITE_IO_BYTE _IOW(IOCTL_MAGIC, 0x8b, uint16_t)

struct ch36x_io_byte {
    uint8_t offset;
    uint8_t byte;
} __attribute__((packed));

static int fd_dev = -1;

static int io_write(uint8_t reg, uint8_t val)
{
    struct ch36x_io_byte s = { reg, val };
    return ioctl(fd_dev, CH36x_WRITE_IO_BYTE, &s);
}

static int io_read(uint8_t reg, uint8_t *val)
{
    struct ch36x_io_byte s = { reg, 0 };
    int r = ioctl(fd_dev, CH36x_READ_IO_BYTE, &s);
    if (r >= 0) *val = s.byte;
    return r;
}

/* ---- FIFO operations ---- */

/*
 * Query available bytes using mode byte.
 * Mode 0x02 = RX count (bytes from STM32 available to read).
 * Count encoding:
 *   b1 & 0x80 == 0: count = b1 & 0x7F          (7-bit, max 127)
 *   b1 & 0x80 != 0: count = (b1&0x3F)<<7 | b2   (13-bit, max 8191)
 */
static int fifo_get_count(uint8_t mode)
{
    if (io_write(0xF1, mode) < 0) return -1;

    uint8_t b1 = 0;
    if (io_read(0x00, &b1) < 0) {
        io_write(0xF1, 0x83);
        return -1;
    }

    int count;
    if (b1 & 0x80) {
        uint8_t b2 = 0;
        if (io_read(0x00, &b2) < 0) {
            io_write(0xF1, 0x83);
            return -1;
        }
        count = ((b1 & 0x3F) << 7) | (b2 & 0x7F);
    } else {
        count = b1 & 0x7F;
    }

    io_write(0xF1, 0x83);
    return count;
}

/* Write bytes to TX FIFO (host→STM32) using mode 0x81. */
static int fifo_write(const uint8_t *buf, int len)
{
    if (io_write(0xF1, 0x81) < 0) return -1;
    for (int i = 0; i < len; i++) {
        if (io_write(0x00, buf[i]) < 0) {
            io_write(0xF1, 0x83);
            return -1;
        }
    }
    io_write(0xF1, 0x83);
    return len;
}

/* Read bytes from RX FIFO (STM32→host) using mode 0x82. */
static int fifo_read_bytes(uint8_t *buf, int count)
{
    if (count <= 0) return 0;
    if (io_write(0xF1, 0x82) < 0) return -1;
    int i;
    for (i = 0; i < count; i++) {
        if (io_read(0x00, &buf[i]) < 0) break;
    }
    io_write(0xF1, 0x83);
    return i;
}

/* ---- TJSG packet builder ---- */

static uint8_t tjsg_checksum(const uint8_t *buf, int len)
{
    uint8_t cs = 0;
    for (int i = 0; i < len; i++) cs += buf[i];
    return cs;
}

static int tjsg_build(uint8_t *buf, int bufsize, uint8_t cmd,
                      const void *payload, int payload_len)
{
    int total = 4 + 2 + 1 + 1 + payload_len + 1;
    if (total > bufsize) return -1;
    buf[0] = 0x54; buf[1] = 0x4A; buf[2] = 0x53; buf[3] = 0x47;
    buf[4] = (uint8_t)(total >> 8);
    buf[5] = (uint8_t)(total & 0xFF);
    buf[6] = cmd;
    buf[7] = 0x00;
    if (payload && payload_len > 0)
        memcpy(buf + 8, payload, payload_len);
    buf[total - 1] = tjsg_checksum(buf, total - 1);
    return total;
}

/* ---- TJSG packet parser ---- */

#define RXBUF_SIZE 8192

typedef struct {
    uint8_t  buf[RXBUF_SIZE];
    int      head;      /* write position */
    int      tail;      /* read position  */
} RxRing;

static RxRing g_rxring;

static void rxring_push(RxRing *r, const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++) {
        r->buf[r->head & (RXBUF_SIZE - 1)] = data[i];
        r->head++;
        if (r->head - r->tail > RXBUF_SIZE)
            r->tail = r->head - RXBUF_SIZE; /* drop oldest */
    }
}

static int rxring_available(const RxRing *r) { return r->head - r->tail; }

static uint8_t rxring_peek(const RxRing *r, int offset)
{
    return r->buf[(r->tail + offset) & (RXBUF_SIZE - 1)];
}

static void rxring_consume(RxRing *r, int n) { r->tail += n; }

/*
 * Try to parse one complete TJSG packet from the ring buffer.
 * Returns packet length if found and written to out_buf, 0 if incomplete, -1 if corrupt.
 */
static int tjsg_parse_one(RxRing *r, uint8_t *out_buf, int out_size)
{
    /* Skip bytes until we find TJSG magic */
    while (rxring_available(r) >= 4) {
        if (rxring_peek(r, 0) == 0x54 && rxring_peek(r, 1) == 0x4A &&
            rxring_peek(r, 2) == 0x53 && rxring_peek(r, 3) == 0x47)
            break;
        rxring_consume(r, 1);
    }

    int avail = rxring_available(r);
    if (avail < 6) return 0; /* need at least magic + 2 len bytes */

    int total = ((int)rxring_peek(r, 4) << 8) | rxring_peek(r, 5);
    if (total < 9 || total > 512) { /* sanity check */
        rxring_consume(r, 1); /* skip bad magic */
        return -1;
    }
    if (avail < total) return 0; /* wait for more data */
    if (total > out_size) { rxring_consume(r, total); return -1; }

    /* Copy full packet */
    for (int i = 0; i < total; i++)
        out_buf[i] = rxring_peek(r, i);

    /* Verify checksum */
    uint8_t cs = tjsg_checksum(out_buf, total - 1);
    if (cs != out_buf[total - 1]) {
        fprintf(stderr, "[rx] bad checksum: got %02X want %02X\n",
                out_buf[total - 1], cs);
        rxring_consume(r, total);
        return -1;
    }

    rxring_consume(r, total);
    return total;
}

/* ---- VCI structs ---- */

#pragma pack(push, 1)
typedef struct {
    uint8_t  FDFlag;
    uint8_t  NON_ISO;
    uint8_t  Timing0;
    uint8_t  Timing1;
    uint8_t  Mode;
    uint8_t  Reserved[3];
} VCI_INIT_CONFIG;     /* 8 bytes */

typedef struct {
    uint32_t ID;
    uint8_t  RemoteFlag;
    uint8_t  ExternFlag;
    uint8_t  FDFlag;
    uint8_t  DataLen;
    uint8_t  Data[64];
} VCI_CAN_OBJ;         /* 72 bytes */
#pragma pack(pop)

/* ---- Protocol operations ---- */

static int sange_init_can(uint32_t channel, const VCI_INIT_CONFIG *cfg)
{
    uint8_t payload[4 + 8];
    uint32_t ch_le = channel;
    memcpy(payload, &ch_le, 4);
    memcpy(payload + 4, cfg, 8);

    uint8_t pkt[256];
    int len = tjsg_build(pkt, sizeof(pkt), 0x01, payload, sizeof(payload));
    if (len < 0) return -1;

    fprintf(stderr, "VCI_InitCan ch=%u pkt[%d]:", channel, len);
    for (int i = 0; i < len; i++) fprintf(stderr, " %02X", pkt[i]);
    fprintf(stderr, "\n");

    return fifo_write(pkt, len);
}

static uint32_t g_tx_seq = 0;

static int sange_transmit(uint32_t channel, const VCI_CAN_OBJ *obj)
{
    uint8_t payload[4 + 4 + 4 + 72];
    uint32_t ch_le    = channel;
    uint32_t count_le = 1;
    uint32_t seq_le   = g_tx_seq++;
    memcpy(payload,      &ch_le,    4);
    memcpy(payload + 4,  &count_le, 4);
    memcpy(payload + 8,  &seq_le,   4);
    memcpy(payload + 12, obj,       72);

    uint8_t pkt[256];
    int len = tjsg_build(pkt, sizeof(pkt), 0x06, payload, sizeof(payload));
    if (len < 0) return -1;
    return fifo_write(pkt, len);
}

/* ---- RX packet decoder ---- */

static void decode_rx_packet(const uint8_t *pkt, int len)
{
    if (len < 9) return;
    uint8_t cmd = pkt[6];
    int payload_len = len - 9; /* TJSG(4)+len(2)+cmd(1)+rsvd(1)+cs(1) = 9 overhead */
    const uint8_t *payload = pkt + 8;

    printf("[RX] cmd=0x%02X len=%d payload=%d bytes", cmd, len, payload_len);

    switch (cmd) {
    case 0x01:
        printf(" → InitCan ACK");
        if (payload_len > 0) {
            printf(" data:");
            for (int i = 0; i < payload_len && i < 16; i++)
                printf(" %02X", payload[i]);
        }
        break;

    case 0x07:
        /* TX done: payload = CanIndex[4] + TxSeq[4] */
        if (payload_len >= 8) {
            uint32_t ch, seq;
            memcpy(&ch,  payload,     4);
            memcpy(&seq, payload + 4, 4);
            printf(" → TX ACK  ch=%u seq=%u", ch, seq);
        } else {
            printf(" → TX ACK  data:");
            for (int i = 0; i < payload_len; i++) printf(" %02X", payload[i]);
        }
        break;

    case 0x05:
        /* Hypothesis: received CAN frame notification
         * Try to decode as: CanIndex[4] + VCI_CAN_OBJ[72] = 76 bytes */
        if (payload_len >= 76) {
            uint32_t ch;
            memcpy(&ch, payload, 4);
            VCI_CAN_OBJ obj;
            memcpy(&obj, payload + 4, sizeof(obj));
            printf(" → RX FRAME ch=%u  ID=0x%X EXT=%d FD=%d len=%d  data:",
                   ch, obj.ID, obj.ExternFlag, obj.FDFlag, obj.DataLen);
            int dl = obj.DataLen > 64 ? 64 : (int)obj.DataLen;
            for (int i = 0; i < dl && i < 16; i++) printf(" %02X", obj.Data[i]);
        } else if (payload_len >= 72) {
            VCI_CAN_OBJ obj;
            memcpy(&obj, payload, sizeof(obj));
            printf(" → RX FRAME  ID=0x%X EXT=%d FD=%d len=%d  data:",
                   obj.ID, obj.ExternFlag, obj.FDFlag, obj.DataLen);
            int dl = obj.DataLen > 64 ? 64 : (int)obj.DataLen;
            for (int i = 0; i < dl && i < 16; i++) printf(" %02X", obj.Data[i]);
        } else {
            printf(" → RX FRAME(?) data:");
            for (int i = 0; i < payload_len && i < 32; i++) printf(" %02X", payload[i]);
        }
        break;

    default:
        printf(" → UNKNOWN data:");
        for (int i = 0; i < payload_len && i < 32; i++)
            printf(" %02X", payload[i]);
        if (payload_len > 32) printf(" ...");
        /* Also try to interpret as received CAN frame if payload >= 72 */
        if (payload_len >= 72) {
            VCI_CAN_OBJ obj;
            memcpy(&obj, payload + payload_len - 72, sizeof(obj));
            printf("\n    (as CAN_OBJ tail) ID=0x%X EXT=%d FD=%d len=%d  data:",
                   obj.ID, obj.ExternFlag, obj.FDFlag, obj.DataLen);
            int dl = obj.DataLen > 64 ? 64 : (int)obj.DataLen;
            for (int i = 0; i < dl && i < 16; i++) printf(" %02X", obj.Data[i]);
        }
        break;
    }
    printf("\n");
    fflush(stdout);
}

/* ---- RX polling ---- */

static void poll_rx(int verbose)
{
    int n = fifo_get_count(0x02);
    if (n <= 0) return;

    uint8_t raw[2048];
    if (n > (int)sizeof(raw)) n = (int)sizeof(raw);
    int rd = fifo_read_bytes(raw, n);
    if (rd <= 0) return;

    if (verbose) {
        printf("[FIFO] %d bytes:", rd);
        for (int i = 0; i < rd && i < 64; i++) printf(" %02X", raw[i]);
        if (rd > 64) printf(" ...");
        printf("\n");
        fflush(stdout);
    }

    rxring_push(&g_rxring, raw, rd);

    uint8_t pktbuf[512];
    int r;
    while ((r = tjsg_parse_one(&g_rxring, pktbuf, sizeof(pktbuf))) != 0) {
        if (r > 0)
            decode_rx_packet(pktbuf, r);
    }
}

/* ---- Signal handling ---- */

static volatile sig_atomic_t g_running = 1;
static void sig_handler(int sig) { (void)sig; g_running = 0; }

/* ---- Main ---- */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --dev <path>    ch36x device (default: /dev/ch36xpci0)\n"
        "  --channel <n>   CAN channel 0 or 1 (default: 0)\n"
        "  --id <hex>      CAN frame ID in hex (default: 0x100)\n"
        "  --len <n>       Data length 0-8 (default: 8)\n"
        "  --timing0 <hex> Nominal bitrate byte (default: 0x60, try 0x50 for 500k)\n"
        "  --timing1 <hex> Data bitrate byte for FD (default: 0x60)\n"
        "  --fd            Enable CAN FD\n"
        "  --ext           Use extended 29-bit ID\n"
        "  --loopback      Enable loopback mode\n"
        "  --rate <us>     TX interval microseconds (0=as fast as possible, default: 100000)\n"
        "  --count <n>     Number of frames to send (0=infinite, default: 0)\n"
        "  --rx-only       Only receive, don't transmit\n"
        "  --tx-only       Transmit only, skip RX decoding\n"
        "  --verbose       Print raw FIFO bytes too\n"
        "  -h, --help      Show this help\n",
        prog);
}

int main(int argc, char *argv[])
{
    const char *dev  = "/dev/ch36xpci0";
    uint32_t channel = 0;
    uint32_t arb_id  = 0x100;
    int  data_len  = 8;
    uint8_t timing0  = 0x60;
    uint8_t timing1  = 0x60;
    int  use_fd    = 0;
    int  use_ext   = 0;
    int  loopback  = 0;
    int  both_ch   = 0;    /* init both channels */
    int  rate_us   = 100000;
    int  tx_count  = 0;
    int  rx_only   = 0;
    int  tx_only   = 0;
    int  verbose   = 0;

    static struct option long_opts[] = {
        {"dev",      required_argument, NULL, 'd'},
        {"channel",  required_argument, NULL, 'c'},
        {"id",       required_argument, NULL, 'i'},
        {"len",      required_argument, NULL, 'l'},
        {"timing0",  required_argument, NULL, '0'},
        {"timing1",  required_argument, NULL, '1'},
        {"fd",       no_argument,       NULL, 'f'},
        {"ext",      no_argument,       NULL, 'e'},
        {"loopback", no_argument,       NULL, 'L'},
        {"both",     no_argument,       NULL, 'b'},
        {"rate",     required_argument, NULL, 'r'},
        {"count",    required_argument, NULL, 'n'},
        {"rx-only",  no_argument,       NULL, 'R'},
        {"tx-only",  no_argument,       NULL, 'T'},
        {"verbose",  no_argument,       NULL, 'v'},
        {"help",     no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "d:c:i:l:0:1:feLbr:n:RTvh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'd': dev      = optarg; break;
        case 'c': channel  = (uint32_t)atoi(optarg); break;
        case 'i': arb_id   = (uint32_t)strtoul(optarg, NULL, 16); break;
        case 'l': data_len = atoi(optarg); break;
        case '0': timing0  = (uint8_t)strtoul(optarg, NULL, 16); break;
        case '1': timing1  = (uint8_t)strtoul(optarg, NULL, 16); break;
        case 'f': use_fd   = 1; break;
        case 'e': use_ext  = 1; break;
        case 'L': loopback = 1; break;
        case 'b': both_ch  = 1; break;
        case 'r': rate_us  = atoi(optarg); break;
        case 'n': tx_count = atoi(optarg); break;
        case 'R': rx_only  = 1; break;
        case 'T': tx_only  = 1; break;
        case 'v': verbose  = 1; break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    if (data_len < 0) data_len = 0;
    if (data_len > (use_fd ? 64 : 8)) data_len = use_fd ? 64 : 8;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    fd_dev = open(dev, O_RDWR);
    if (fd_dev < 0) {
        fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
        return 1;
    }
    fprintf(stderr, "Opened %s\n", dev);

    /* Set IO speed register */
    io_write(0xFA, 0);

    /* Drain stale data */
    {
        int n = fifo_get_count(0x02);
        if (n > 0) {
            fprintf(stderr, "Draining %d stale bytes\n", n);
            uint8_t trash[512];
            while (n > 0) {
                int rd = n > (int)sizeof(trash) ? (int)sizeof(trash) : n;
                fifo_read_bytes(trash, rd);
                n -= rd;
            }
        }
    }

    /* VCI_InitCan */
    VCI_INIT_CONFIG cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.FDFlag  = use_fd ? 1 : 0;
    cfg.NON_ISO = 0;
    cfg.Timing0 = timing0;
    cfg.Timing1 = use_fd ? timing1 : 0;
    cfg.Mode    = loopback ? 1 : 0;

    fprintf(stderr, "VCI_InitCan ch=%u FD=%d timing0=0x%02X timing1=0x%02X mode=%s\n",
            channel, cfg.FDFlag, cfg.Timing0, cfg.Timing1,
            loopback ? "loopback" : "normal");

    if (sange_init_can(channel, &cfg) < 0) {
        fprintf(stderr, "sange_init_can failed: %s\n", strerror(errno));
        close(fd_dev);
        return 1;
    }

    /* Optionally init the other channel too (for cross-channel testing) */
    if (both_ch) {
        uint32_t ch2 = channel ^ 1;  /* toggle between 0 and 1 */
        VCI_INIT_CONFIG cfg2 = cfg;
        cfg2.Mode = 0;  /* other channel in normal RX mode */
        fprintf(stderr, "VCI_InitCan ch=%u FD=%d timing0=0x%02X (second channel, normal mode)\n",
                ch2, cfg2.FDFlag, cfg2.Timing0);
        usleep(50000);
        sange_init_can(ch2, &cfg2);
    }

    /* Wait for and display InitCan response(s) */
    usleep(200000);  /* 200ms */
    poll_rx(verbose);

    fprintf(stderr, "\nRunning (Ctrl+C to stop)\n");
    fprintf(stderr, "  TX: %s  ID=0x%X  len=%d  rate=%s\n",
            rx_only ? "disabled" : "enabled",
            arb_id, data_len,
            rate_us == 0 ? "max" : "");
    if (!rx_only && rate_us > 0)
        fprintf(stderr, "      %d us/frame\n", rate_us);

    uint64_t tx_frames = 0;
    uint64_t rx_packets = 0;
    uint8_t  data_byte  = 0;

    struct timespec ts_last;
    clock_gettime(CLOCK_MONOTONIC, &ts_last);

    while (g_running) {
        /* Transmit */
        if (!rx_only && (tx_count == 0 || (int)tx_frames < tx_count)) {
            VCI_CAN_OBJ obj;
            memset(&obj, 0, sizeof(obj));
            obj.ID         = arb_id;
            obj.ExternFlag = use_ext ? 1 : 0;
            obj.FDFlag     = use_fd ? 1 : 0;
            obj.DataLen    = (uint8_t)data_len;
            for (int i = 0; i < data_len; i++)
                obj.Data[i] = (uint8_t)(data_byte + i);
            data_byte++;

            if (sange_transmit(channel, &obj) > 0)
                tx_frames++;

            if (tx_count > 0 && (int)tx_frames >= tx_count) {
                fprintf(stderr, "Sent %d frames, waiting for RX...\n", tx_count);
                usleep(500000);
                poll_rx(verbose);
                break;
            }

            if (rate_us > 0)
                usleep(rate_us);
        }

        /* Receive */
        if (!tx_only) {
            int n = fifo_get_count(0x02);
            if (n > 0) {
                uint8_t raw[2048];
                if (n > (int)sizeof(raw)) n = (int)sizeof(raw);
                int rd = fifo_read_bytes(raw, n);
                if (rd > 0) {
                    if (verbose) {
                        printf("[FIFO] %d bytes:", rd);
                        for (int i = 0; i < rd && i < 64; i++) printf(" %02X", raw[i]);
                        if (rd > 64) printf(" ...");
                        printf("\n");
                        fflush(stdout);
                    }
                    rxring_push(&g_rxring, raw, rd);
                    uint8_t pktbuf[512];
                    int r;
                    while ((r = tjsg_parse_one(&g_rxring, pktbuf, sizeof(pktbuf))) != 0) {
                        if (r > 0) {
                            decode_rx_packet(pktbuf, r);
                            rx_packets++;
                        }
                    }
                }
            }
        }

        /* Stats every 5 seconds */
        struct timespec ts_now;
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double elapsed = (ts_now.tv_sec - ts_last.tv_sec)
                       + (ts_now.tv_nsec - ts_last.tv_nsec) / 1e9;
        if (elapsed >= 5.0) {
            fprintf(stderr, "[stats] TX=%llu frames  RX=%llu packets\n",
                    (unsigned long long)tx_frames,
                    (unsigned long long)rx_packets);
            ts_last = ts_now;
        }
    }

    fprintf(stderr, "\nDone (TX=%llu frames  RX=%llu packets)\n",
            (unsigned long long)tx_frames, (unsigned long long)rx_packets);
    close(fd_dev);
    return 0;
}

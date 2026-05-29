/*
 * net_damiao.c - DaMiao motor CAN throughput tester.
 *
 * Sends MIT zero-torque frames to a DaMiao motor and receives feedback
 * simultaneously. Reports request/response rate per second.
 *
 * TX thread: MIT control (kp=0 kd=0 tau=0) → motor slave ID
 * RX thread: motor state feedback ← motor master ID
 *
 * Usage:
 *   sudo ./net_damiao --iface can0 --id 1 --master-id 0x101
 *
 * Compile:
 *   gcc -O2 -Wall -o net_damiao net_damiao.c -lpthread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <stdatomic.h>

static volatile sig_atomic_t g_running = 1;

static atomic_uint_fast64_t g_tx_count = 0;
static atomic_uint_fast64_t g_rx_count = 0;

/* Last decoded motor state — written by RX thread, read by main */
static atomic_int g_last_status   = 0;
static atomic_int g_last_q_raw    = 0;
static atomic_int g_last_dq_raw   = 0;
static atomic_int g_last_tau_raw  = 0;
static atomic_int g_last_mos_temp = 0;

static int      g_sock      = -1;
static uint16_t g_slave_id  = 1;
static uint32_t g_master_id = 0;   /* 0 = accept any */
static int      g_rate_us   = 0;

static void sig_handler(int sig) { (void)sig; g_running = 0; }

/* MIT zero-torque frame: kp=0, kd=0, q=0, dq=0, tau=0
 *
 * float_to_uint(0, -q_max, q_max, 16)  → 0x7FFF  (midpoint)
 * float_to_uint(0, -dq_max,dq_max, 12) → 0x7FF   (midpoint)
 * kp=0 → 0x000
 * kd=0 → 0x000
 * tau=0 → 0x7FF (midpoint)
 *
 * word1 = q(16)               = 0x7FFF
 * word2 = dq(12)|kp_hi(4)     = (0x7FF<<4)|0 = 0x7FF0
 * word3 = kp_lo(8)|kd_hi(8)   = 0x0000
 * word4 = kd_lo(4)|tau(12)    = 0x07FF
 *
 * Bytes: 7F FF 7F F0 00 00 07 FF
 */
static const uint8_t MIT_ZERO_DATA[8] = {
    0x7F, 0xFF, 0x7F, 0xF0, 0x00, 0x00, 0x07, 0xFF
};

static void build_mit_frame(struct can_frame *f)
{
    memset(f, 0, sizeof(*f));
    f->can_id  = g_slave_id;
    f->can_dlc = 8;
    memcpy(f->data, MIT_ZERO_DATA, 8);
}

static void send_enable(void)
{
    struct can_frame f;
    memset(&f, 0, sizeof(f));
    f.can_id  = g_slave_id;
    f.can_dlc = 8;
    memset(f.data, 0xFF, 7);
    f.data[7] = 0xFC;
    if (write(g_sock, &f, sizeof(f)) < 0)
        perror("enable write");
    else
        fprintf(stderr, "Enable:    sent\n");
    usleep(50000);
}

static void send_disable(void)
{
    struct can_frame f;
    memset(&f, 0, sizeof(f));
    f.can_id  = g_slave_id;
    f.can_dlc = 8;
    memset(f.data, 0xFF, 7);
    f.data[7] = 0xFD;
    if (write(g_sock, &f, sizeof(f)) < 0)
        perror("disable write");
    fprintf(stderr, "Disable:   sent\n");
}

static void *tx_thread(void *arg)
{
    (void)arg;
    struct can_frame frame;
    build_mit_frame(&frame);

    struct timespec ts_next;
    clock_gettime(CLOCK_MONOTONIC, &ts_next);

    while (g_running) {
        ssize_t n = write(g_sock, &frame, sizeof(frame));
        if (n < 0) {
            if (errno == EINTR) break;
            perror("tx write");
            break;
        }
        atomic_fetch_add(&g_tx_count, 1);

        if (g_rate_us > 0) {
            ts_next.tv_nsec += g_rate_us * 1000;
            if (ts_next.tv_nsec >= 1000000000L) {
                ts_next.tv_sec++;
                ts_next.tv_nsec -= 1000000000L;
            }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts_next, NULL);
        }
    }
    return NULL;
}

/* Decode 8-byte motor state response.
 * Format (big-endian):
 *   byte0:    (status<<4) | slave_id
 *   bytes1-2: position uint16
 *   byte3:    vel[11:4]
 *   byte4:    vel[3:0] | tau[11:8]
 *   byte5:    tau[7:0]
 *   byte6:    temp_mos
 *   byte7:    temp_rotor
 */
static void decode_state(const uint8_t *d)
{
    int status = (d[0] >> 4) & 0xF;
    int q_raw  = (d[1] << 8) | d[2];
    int dq_raw = (d[3] << 4) | ((d[4] >> 4) & 0xF);
    int tau_raw= ((d[4] & 0xF) << 8) | d[5];
    int mos_t  = d[6];

    atomic_store(&g_last_status,   status);
    atomic_store(&g_last_q_raw,    q_raw);
    atomic_store(&g_last_dq_raw,   dq_raw);
    atomic_store(&g_last_tau_raw,  tau_raw);
    atomic_store(&g_last_mos_temp, mos_t);
}

static void *rx_thread(void *arg)
{
    (void)arg;
    struct can_frame frame;

    while (g_running) {
        ssize_t n = read(g_sock, &frame, sizeof(frame));
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("rx read");
            break;
        }
        if (n < (ssize_t)sizeof(struct can_frame)) continue;

        uint32_t id = frame.can_id & CAN_SFF_MASK;
        if (g_master_id != 0 && id != g_master_id)
            continue;

        if (frame.can_dlc == 8)
            decode_state(frame.data);

        atomic_fetch_add(&g_rx_count, 1);
    }
    return NULL;
}

static const char *status_str(int s)
{
    switch (s) {
    case 0x0: return "DISABLED";
    case 0x1: return "ENABLED";
    case 0x8: return "OVERVOLTAGE";
    case 0x9: return "UNDERVOLTAGE";
    case 0xA: return "OVERCURRENT";
    case 0xB: return "MOS_OVERTEMP";
    case 0xC: return "COIL_OVERTEMP";
    case 0xD: return "COMM_LOSS";
    case 0xE: return "OVERLOAD";
    default:  return "UNKNOWN";
    }
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s --iface <dev> [options]\n"
        "\n"
        "Options:\n"
        "  --iface <dev>      CAN interface (e.g. can0)        [required]\n"
        "  --id <n>           Motor slave ID, decimal (default: 1)\n"
        "  --master-id <hex>  Motor feedback CAN ID (default: 0 = any)\n"
        "  --rate <us>        TX interval in microseconds (default: 0 = max)\n"
        "  --enable           Send enable command before starting\n"
        "  --disable          Send disable command on exit\n"
        "  --priority <n>     SCHED_FIFO RT priority 1-99 (default: 80, 0 = off)\n"
        "  -h, --help         Show this help\n",
        prog);
}

int main(int argc, char *argv[])
{
    const char *iface    = NULL;
    int do_enable        = 0;
    int do_disable       = 0;
    int rt_priority      = 80;

    static struct option long_opts[] = {
        {"iface",     required_argument, NULL, 'n'},
        {"id",        required_argument, NULL, 'i'},
        {"master-id", required_argument, NULL, 'm'},
        {"rate",      required_argument, NULL, 'r'},
        {"enable",    no_argument,       NULL, 'e'},
        {"disable",   no_argument,       NULL, 'd'},
        {"priority",  required_argument, NULL, 'P'},
        {"help",      no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "n:i:m:r:edP:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'n': iface       = optarg; break;
        case 'i': g_slave_id  = (uint16_t)atoi(optarg); break;
        case 'm': g_master_id = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'r': g_rate_us   = atoi(optarg); break;
        case 'e': do_enable   = 1; break;
        case 'd': do_disable  = 1; break;
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

    g_sock = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (g_sock < 0) { perror("socket"); return 1; }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(g_sock, SIOCGIFINDEX, &ifr) < 0) { perror("ioctl"); return 1; }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(g_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }

    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0)
        fprintf(stderr, "Warning: mlockall failed: %s\n", strerror(errno));

    if (rt_priority > 0) {
        struct sched_param sp = { .sched_priority = rt_priority };
        if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0)
            fprintf(stderr, "Warning: SCHED_FIFO failed (need root?): %s\n", strerror(errno));
        else
            fprintf(stderr, "RT:        SCHED_FIFO priority %d\n", rt_priority);
    }

    fprintf(stderr, "Interface: %s\n", iface);
    fprintf(stderr, "Slave ID:  0x%X (%u)\n", g_slave_id, g_slave_id);
    if (g_master_id)
        fprintf(stderr, "Master ID: 0x%X\n", g_master_id);
    else
        fprintf(stderr, "Master ID: any\n");
    fprintf(stderr, "Rate:      %s\n",
            g_rate_us > 0 ? "" : "full speed (no delay)");
    if (g_rate_us > 0) fprintf(stderr, "           %d us/frame\n", g_rate_us);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (do_enable)
        send_enable();

    fprintf(stderr, "Running... (Ctrl+C to stop)\n\n");

    pthread_t tid_tx, tid_rx;
    pthread_create(&tid_rx, NULL, rx_thread, NULL);
    pthread_create(&tid_tx, NULL, tx_thread, NULL);

    uint64_t last_tx = 0, last_rx = 0;
    struct timespec ts_last, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_last);

    while (g_running) {
        sleep(1);

        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double elapsed = (ts_now.tv_sec - ts_last.tv_sec)
                       + (ts_now.tv_nsec - ts_last.tv_nsec) / 1e9;

        uint64_t tx = atomic_load(&g_tx_count);
        uint64_t rx = atomic_load(&g_rx_count);
        double req_s = (tx - last_tx) / elapsed;
        double rsp_s = (rx - last_rx) / elapsed;
        double rate  = (req_s > 0) ? (rsp_s / req_s) * 100.0 : 0.0;

        int status  = atomic_load(&g_last_status);
        int q_raw   = atomic_load(&g_last_q_raw);
        int dq_raw  = atomic_load(&g_last_dq_raw);
        int tau_raw = atomic_load(&g_last_tau_raw);
        int mos_t   = atomic_load(&g_last_mos_temp);

        printf("[%7lu tx / %7lu rx]  %6.0f req/s  %6.0f rsp/s  %.0f%%  |"
               "  %-12s  pos=%d  vel=%d  tau=%d  mos=%d°C\n",
               (unsigned long)tx, (unsigned long)rx,
               req_s, rsp_s, rate,
               status_str(status), q_raw, dq_raw, tau_raw, mos_t);
        fflush(stdout);

        last_tx  = tx;
        last_rx  = rx;
        ts_last  = ts_now;
    }

    pthread_join(tid_tx, NULL);
    pthread_join(tid_rx, NULL);

    if (do_disable)
        send_disable();

    fprintf(stderr, "\nDone. tx=%lu rx=%lu\n",
            (unsigned long)atomic_load(&g_tx_count),
            (unsigned long)atomic_load(&g_rx_count));

    close(g_sock);
    return 0;
}

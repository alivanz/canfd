#ifndef SANGE_H
#define SANGE_H

/*
 * sange.h - Shared definitions for SanGe PCIe CAN-FD driver
 *
 * Hardware: CH367 PCIe bridge → CH424Q FIFO → STM32H750 CAN controller
 * Kernel driver: ch36x_linux (/dev/ch36xpci0)
 *
 * FIFO reg 0xF1 modes:
 *   0x02 = query available RX bytes
 *   0x81 = assert CS for TX write
 *   0x82 = read RX bytes
 *   0x83 = deassert CS
 */

#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

/* ---- CH36x ioctl ---- */
#define IOCTL_MAGIC         'P'
#define CH36x_READ_IO_BYTE  _IOR(IOCTL_MAGIC, 0x88, uint16_t)
#define CH36x_WRITE_IO_BYTE _IOW(IOCTL_MAGIC, 0x8b, uint16_t)

struct ch36x_io_byte { uint8_t offset; uint8_t byte; } __attribute__((packed));

/* ---- VCI structs ---- */
#pragma pack(push, 1)
typedef struct {
    uint8_t  FDFlag;
    uint8_t  NON_ISO;
    uint8_t  Timing0;
    uint8_t  Timing1;
    uint8_t  Mode;
    uint8_t  Reserved[3];
} VCI_INIT_CONFIG;   /* 8 bytes */

typedef struct {
    uint32_t ID;
    uint8_t  RemoteFlag;
    uint8_t  ExternFlag;
    uint8_t  FDFlag;
    uint8_t  DataLen;
    uint8_t  Data[64];
} VCI_CAN_OBJ;       /* 72 bytes */
#pragma pack(pop)

/* ---- TJSG commands ---- */
#define CMD_INIT_CAN   0x01  /* host→device: init channel          */
#define CMD_SET_FILTER 0x10  /* host→device: set acceptance filter  */
#define CMD_TRANSMIT   0x06  /* host→device: send CAN frame         */
#define CMD_RX_FRAME   0x06  /* device→host: received CAN frame     */
#define CMD_TX_ACK     0x07  /* device→host: TX done (CanIdx+Seq)   */

/* ---- Low-level IO (caller holds flock) ---- */

static inline int io_write(int fd, uint8_t reg, uint8_t val)
{
    struct ch36x_io_byte s = { reg, val };
    return ioctl(fd, CH36x_WRITE_IO_BYTE, &s);
}

static inline int io_read(int fd, uint8_t reg, uint8_t *val)
{
    struct ch36x_io_byte s = { reg, 0 };
    int r = ioctl(fd, CH36x_READ_IO_BYTE, &s);
    if (r >= 0) *val = s.byte;
    return r;
}

/* ---- FIFO primitives (caller holds flock) ---- */

static inline int fifo_get_count(int fd, uint8_t mode)
{
    if (io_write(fd, 0xF1, mode) < 0) return -1;
    uint8_t b1 = 0;
    if (io_read(fd, 0x00, &b1) < 0) { io_write(fd, 0xF1, 0x83); return -1; }
    int count;
    if (b1 & 0x80) {
        uint8_t b2 = 0;
        if (io_read(fd, 0x00, &b2) < 0) { io_write(fd, 0xF1, 0x83); return -1; }
        count = ((b1 & 0x3F) << 7) | (b2 & 0x7F);
    } else {
        count = b1 & 0x7F;
    }
    io_write(fd, 0xF1, 0x83);
    return count;
}

static inline int fifo_write(int fd, const uint8_t *buf, int len)
{
    if (io_write(fd, 0xF1, 0x81) < 0) return -1;
    for (int i = 0; i < len; i++) {
        if (io_write(fd, 0x00, buf[i]) < 0) { io_write(fd, 0xF1, 0x83); return -1; }
    }
    io_write(fd, 0xF1, 0x83);
    return len;
}

static inline int fifo_read_bytes(int fd, uint8_t *buf, int count)
{
    if (count <= 0) return 0;
    if (io_write(fd, 0xF1, 0x82) < 0) return -1;
    int i;
    for (i = 0; i < count; i++) {
        if (io_read(fd, 0x00, &buf[i]) < 0) break;
    }
    io_write(fd, 0xF1, 0x83);
    return i;
}

/* ---- TJSG packet builder ---- */

static inline uint8_t tjsg_checksum(const uint8_t *buf, int len)
{
    uint8_t cs = 0;
    for (int i = 0; i < len; i++) cs += buf[i];
    return cs;
}

/* Returns total packet length. total = 4(TJSG)+2(len)+1(cmd)+1(rsvd)+payload+1(cs) */
static inline int tjsg_build(uint8_t *buf, int bufsize, uint8_t cmd,
                              const void *payload, int payload_len)
{
    int total = 9 + payload_len;
    if (total > bufsize) return -1;
    buf[0] = 0x54; buf[1] = 0x4A; buf[2] = 0x53; buf[3] = 0x47;
    buf[4] = (uint8_t)(total >> 8);
    buf[5] = (uint8_t)(total & 0xFF);
    buf[6] = cmd;
    buf[7] = 0x00;
    if (payload && payload_len > 0) memcpy(buf + 8, payload, payload_len);
    buf[total - 1] = tjsg_checksum(buf, total - 1);
    return total;
}

/* ---- VCI filter config (accept all: all zeros = no mask applied) ---- */
#pragma pack(push, 1)
typedef struct {
    uint8_t  FilterType;   /* 0=dual, 1=single */
    uint8_t  Reserved[3];
    uint32_t AccCode;      /* acceptance code  */
    uint32_t AccMask;      /* 0xFFFFFFFF = accept all */
    uint32_t Reserved2;
} VCI_FILTER_CONFIG;      /* 16 bytes */
#pragma pack(pop)

/* ---- High-level ops (each acquires flock internally) ---- */

static inline int sange_set_filter(int fd, uint32_t channel)
{
    uint8_t payload[20];
    VCI_FILTER_CONFIG fc;
    memset(&fc, 0, sizeof(fc));
    fc.AccMask = 0xFFFFFFFF;  /* accept all IDs */

    memcpy(payload,      &channel, 4);
    memcpy(payload + 4,  &fc,     16);

    uint8_t pkt[64];
    int len = tjsg_build(pkt, sizeof(pkt), CMD_SET_FILTER, payload, sizeof(payload));
    if (len < 0) return -1;

    flock(fd, LOCK_EX);
    int r = fifo_write(fd, pkt, len);
    flock(fd, LOCK_UN);
    return r;
}

static inline int sange_init_can(int fd, uint32_t channel,
                                 const VCI_INIT_CONFIG *cfg)
{
    uint8_t payload[12];
    memcpy(payload,     &channel, 4);
    memcpy(payload + 4, cfg,      8);

    uint8_t pkt[64];
    int len = tjsg_build(pkt, sizeof(pkt), CMD_INIT_CAN, payload, sizeof(payload));
    if (len < 0) return -1;

    flock(fd, LOCK_EX);
    int r = fifo_write(fd, pkt, len);
    flock(fd, LOCK_UN);
    return r;
}

static inline int sange_transmit(int fd, uint32_t channel,
                                 const VCI_CAN_OBJ *obj, uint32_t seq)
{
    uint8_t payload[84];
    uint32_t count = 1;
    memcpy(payload,      &channel, 4);
    memcpy(payload + 4,  &count,   4);
    memcpy(payload + 8,  &seq,     4);
    memcpy(payload + 12, obj,     72);

    uint8_t pkt[128];
    int len = tjsg_build(pkt, sizeof(pkt), CMD_TRANSMIT, payload, sizeof(payload));
    if (len < 0) return -1;

    flock(fd, LOCK_EX);
    int r = fifo_write(fd, pkt, len);
    flock(fd, LOCK_UN);
    return r;
}

/* Read up to max_bytes from FIFO into buf. Returns bytes read (0 if none). */
static inline int sange_poll_rx(int fd, uint8_t *buf, int max_bytes)
{
    flock(fd, LOCK_EX);
    int n = fifo_get_count(fd, 0x02);
    if (n <= 0) { flock(fd, LOCK_UN); return 0; }
    if (n > max_bytes) n = max_bytes;
    int rd = fifo_read_bytes(fd, buf, n);
    flock(fd, LOCK_UN);
    return rd;
}

/* ---- TJSG ring buffer parser ---- */
#define SANGE_RING_SIZE 8192

typedef struct {
    uint8_t  buf[SANGE_RING_SIZE];
    int      head;
    int      tail;
} SangeRing;

static inline void sange_ring_push(SangeRing *r, const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++) {
        r->buf[r->head & (SANGE_RING_SIZE - 1)] = data[i];
        r->head++;
        if (r->head - r->tail > SANGE_RING_SIZE)
            r->tail = r->head - SANGE_RING_SIZE;
    }
}

static inline int sange_ring_peek(const SangeRing *r, int off)
{
    if (r->head - r->tail <= off) return -1;
    return r->buf[(r->tail + off) & (SANGE_RING_SIZE - 1)];
}

static inline void sange_ring_consume(SangeRing *r, int n) { r->tail += n; }
static inline int  sange_ring_avail(const SangeRing *r)    { return r->head - r->tail; }

/*
 * Parse one complete TJSG packet from ring into out_buf[out_size].
 * Returns: packet length (success), 0 (incomplete), -1 (corrupt/skipped).
 */
static inline int sange_parse_one(SangeRing *r, uint8_t *out_buf, int out_size)
{
    /* skip to TJSG magic */
    while (sange_ring_avail(r) >= 4) {
        if (sange_ring_peek(r, 0) == 0x54 && sange_ring_peek(r, 1) == 0x4A &&
            sange_ring_peek(r, 2) == 0x53 && sange_ring_peek(r, 3) == 0x47)
            break;
        sange_ring_consume(r, 1);
    }
    if (sange_ring_avail(r) < 6) return 0;

    int total = (sange_ring_peek(r, 4) << 8) | sange_ring_peek(r, 5);
    if (total < 9 || total > 512) { sange_ring_consume(r, 1); return -1; }
    if (sange_ring_avail(r) < total) return 0;
    if (total > out_size) { sange_ring_consume(r, total); return -1; }

    for (int i = 0; i < total; i++)
        out_buf[i] = (uint8_t)sange_ring_peek(r, i);

    uint8_t cs = tjsg_checksum(out_buf, total - 1);
    if (cs != out_buf[total - 1]) {
        fprintf(stderr, "[sange] bad checksum: got %02X want %02X\n",
                out_buf[total - 1], cs);
        sange_ring_consume(r, total);
        return -1;
    }
    sange_ring_consume(r, total);
    return total;
}

#endif /* SANGE_H */

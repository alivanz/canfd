#ifndef CANTS_H
#define CANTS_H

#include <stdint.h>
#include <time.h>

#define CANTS_SIZE 8  /* bytes needed in payload for timestamp */

/* Encode current CLOCK_MONOTONIC time into first 8 bytes of data */
static inline void cants_encode(uint8_t *data)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    __builtin_memcpy(data, &ns, 8);
}

/* Decode timestamp from first 8 bytes, return latency in nanoseconds */
static inline int64_t cants_decode(const uint8_t *data)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    uint64_t sent;
    __builtin_memcpy(&sent, data, 8);
    return (int64_t)(now - sent);
}

#endif /* CANTS_H */

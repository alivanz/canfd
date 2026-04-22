#ifndef CANTS_H
#define CANTS_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>

#define CANTS_SIZE     8      /* bytes needed in payload for timestamp */
#define CANTS_BUF_SIZE 65536  /* max samples per stats interval */

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

/* Latency stats buffer */
typedef struct {
    int64_t *samples;   /* malloc'd array of CANTS_BUF_SIZE */
    size_t   count;
} CantsStats;

static inline int cants_stats_init(CantsStats *s)
{
    s->samples = (int64_t *)malloc(CANTS_BUF_SIZE * sizeof(int64_t));
    s->count   = 0;
    return s->samples ? 0 : -1;
}

static inline void cants_stats_add(CantsStats *s, int64_t ns)
{
    if (s->count < CANTS_BUF_SIZE)
        s->samples[s->count++] = ns;
}

static inline int cants_cmp(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

/* Print percentile stats and reset. Format: min/p10/p50/p90/max (us) */
static inline void cants_stats_print(CantsStats *s)
{
    if (s->count == 0) return;

    qsort(s->samples, s->count, sizeof(int64_t), cants_cmp);

    double min  = s->samples[0] / 1000.0;
    double p10  = s->samples[(size_t)(s->count * 0.10)] / 1000.0;
    double p50  = s->samples[(size_t)(s->count * 0.50)] / 1000.0;
    double p90  = s->samples[(size_t)(s->count * 0.90)] / 1000.0;
    double max  = s->samples[s->count - 1] / 1000.0;

    printf("  | latency min/p10/p50/p90/max: %.1f/%.1f/%.1f/%.1f/%.1f us",
           min, p10, p50, p90, max);

    s->count = 0;
}

#endif /* CANTS_H */

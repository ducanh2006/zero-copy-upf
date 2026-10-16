#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <x86intrin.h>

#include "mbuf_model.h"
#include "outer_header.h"
#include "deepcopy.h"
#include "zerocopy.h"

#define STREAM_RING_SIZE 65536

static inline uint64_t rdtscp_begin(unsigned *aux)
{
    _mm_mfence();
    uint64_t t = __rdtscp(aux);
    _mm_mfence();
    return t;
}

static inline uint64_t rdtscp_end(unsigned *aux)
{
    _mm_mfence();
    uint64_t t = __rdtscp(aux);
    _mm_mfence();
    return t;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    if (x < y)
        return -1;
    if (x > y)
        return 1;
    return 0;
}

typedef struct {
    double avg_cycles;
    double avg_ns;
    double mpps;
    uint64_t p50;
    uint64_t p95;
    uint64_t p99;
} Stats;

static Stats compute_stats(uint64_t *cycles, uint64_t total_ns, int iters)
{
    Stats s;
    uint64_t sum = 0;
    for (int i = 0; i < iters; i++)
        sum += cycles[i];
    s.avg_cycles = (double)sum / (double)iters;
    s.avg_ns = (double)total_ns / (double)iters;
    s.mpps = (s.avg_ns > 0) ? (1000.0 / s.avg_ns) : 0.0;
    qsort(cycles, (size_t)iters, sizeof(uint64_t), cmp_u64);
    s.p50 = cycles[(size_t)(iters * 0.50)];
    s.p95 = cycles[(size_t)(iters * 0.95)];
    s.p99 = cycles[(size_t)(iters * 0.99)];
    return s;
}

static void fill_payload(Mbuf *m, int seed)
{
    uint8_t *p = (uint8_t *)m->buf_addr + m->data_off;
    for (int i = 0; i < 800; i++)
        p[i] = (uint8_t)((i * 31 + seed) & 0xFF);
    m->data_len = 800;
    m->pkt_len = 800;
}

/* Benchmark deepcopy in hot mode: single payload reused. */
static Stats bench_deepcopy_hot(Mbuf *P, Mempool *pool, int iters,
                                uint64_t *cycles, uint64_t *total_ns)
{
    uint64_t t0 = now_ns();
    for (int i = 0; i < iters; i++) {
        Mbuf *o1 = NULL;
        Mbuf *o2 = NULL;
        unsigned aux = 0;
        uint64_t c0 = rdtscp_begin(&aux);
        size_t b = duplicate_deepcopy(P, &o1, &o2, pool);
        uint64_t c1 = rdtscp_end(&aux);
        (void)b;
        cycles[i] = c1 - c0;
        if (o1)
            mbuf_free(o1);
        if (o2)
            mbuf_free(o2);
    }
    uint64_t t1 = now_ns();
    *total_ns = t1 - t0;
    /* Recompute avg_ns from cycles? No: use wall total (includes frees).
     * To isolate duplicate cost, also compute from cycles via measured GHz?
     * We report wall-based avg_ns (conservative) + cycle stats. */
    return compute_stats(cycles, *total_ns, iters);
}

/* Benchmark zerocopy in hot mode. */
static Stats bench_zerocopy_hot(Mbuf *P, Mempool *hpool, int iters,
                                uint64_t *cycles, uint64_t *total_ns)
{
    uint64_t t0 = now_ns();
    for (int i = 0; i < iters; i++) {
        Mbuf *h1 = NULL;
        Mbuf *h2 = NULL;
        unsigned aux = 0;
        uint64_t c0 = rdtscp_begin(&aux);
        size_t b = duplicate_zerocopy(P, hpool, &h1, &h2);
        uint64_t c1 = rdtscp_end(&aux);
        (void)b;
        cycles[i] = c1 - c0;
        if (h1)
            mbuf_free(h1);
        if (h2)
            mbuf_free(h2);
    }
    uint64_t t1 = now_ns();
    *total_ns = t1 - t0;
    return compute_stats(cycles, *total_ns, iters);
}

/* Stream mode: rotate over ring to thrash cache (> L3 30MB). */
static Stats bench_deepcopy_stream(Mbuf **ring, int ring_n, Mempool *pool,
                                   int iters, uint64_t *cycles, uint64_t *total_ns)
{
    uint64_t t0 = now_ns();
    for (int i = 0; i < iters; i++) {
        Mbuf *P = ring[i % ring_n];
        Mbuf *o1 = NULL;
        Mbuf *o2 = NULL;
        unsigned aux = 0;
        uint64_t c0 = rdtscp_begin(&aux);
        size_t b = duplicate_deepcopy(P, &o1, &o2, pool);
        uint64_t c1 = rdtscp_end(&aux);
        (void)b;
        cycles[i] = c1 - c0;
        if (o1)
            mbuf_free(o1);
        if (o2)
            mbuf_free(o2);
    }
    uint64_t t1 = now_ns();
    *total_ns = t1 - t0;
    return compute_stats(cycles, *total_ns, iters);
}

static Stats bench_zerocopy_stream(Mbuf **ring, int ring_n, Mempool *hpool,
                                   int iters, uint64_t *cycles, uint64_t *total_ns)
{
    uint64_t t0 = now_ns();
    for (int i = 0; i < iters; i++) {
        Mbuf *P = ring[i % ring_n];
        Mbuf *h1 = NULL;
        Mbuf *h2 = NULL;
        unsigned aux = 0;
        uint64_t c0 = rdtscp_begin(&aux);
        size_t b = duplicate_zerocopy(P, hpool, &h1, &h2);
        uint64_t c1 = rdtscp_end(&aux);
        (void)b;
        cycles[i] = c1 - c0;
        if (h1)
            mbuf_free(h1);
        if (h2)
            mbuf_free(h2);
    }
    uint64_t t1 = now_ns();
    *total_ns = t1 - t0;
    return compute_stats(cycles, *total_ns, iters);
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--mode hot|stream] [--iters N] [--payload 800]\n"
            "  hot:    single payload reused (L1 hot), default iters 200000\n"
            "  stream: ring of 65536 payloads (~55MB > L3 30MB), default iters 100000\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *mode = "hot";
    int iters = 0; /* 0 = default per mode */
    int payload_len = 800;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
        } else if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc) {
            iters = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--payload") == 0 && i + 1 < argc) {
            payload_len = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    int is_hot = (strcmp(mode, "hot") == 0);
    int is_stream = (strcmp(mode, "stream") == 0);
    if (!is_hot && !is_stream) {
        fprintf(stderr, "invalid --mode %s (expected hot|stream)\n", mode);
        return 1;
    }
    if (payload_len != 800) {
        fprintf(stderr, "warning: payload %d != 800, forcing 800 for comparability\n",
                payload_len);
        payload_len = 800;
    }
    (void)payload_len;
    if (iters <= 0)
        iters = is_hot ? 200000 : 100000;
    if (iters < 1000)
        iters = 1000;

    uint64_t *c_dc = (uint64_t *)malloc((size_t)iters * sizeof(uint64_t));
    uint64_t *c_zc = (uint64_t *)malloc((size_t)iters * sizeof(uint64_t));
    if (!c_dc || !c_zc) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }

    Stats s_dc, s_zc;
    uint64_t total_dc = 0, total_zc = 0;

    if (is_hot) {
        Mempool *payload_pool = mempool_create(8192, 0);
        Mempool *header_pool = mempool_create(8192, 1);
        if (!payload_pool || !header_pool) {
            fprintf(stderr, "pool create failed\n");
            return 1;
        }
        Mbuf *P = mbuf_alloc(payload_pool);
        if (!P) {
            fprintf(stderr, "payload alloc failed\n");
            return 1;
        }
        fill_payload(P, 7);

        /* Warmup (not measured). */
        for (int i = 0; i < 1000; i++) {
            Mbuf *o1 = NULL, *o2 = NULL;
            duplicate_deepcopy(P, &o1, &o2, payload_pool);
            if (o1)
                mbuf_free(o1);
            if (o2)
                mbuf_free(o2);
            Mbuf *h1 = NULL, *h2 = NULL;
            duplicate_zerocopy(P, header_pool, &h1, &h2);
            if (h1)
                mbuf_free(h1);
            if (h2)
                mbuf_free(h2);
        }

        s_dc = bench_deepcopy_hot(P, payload_pool, iters, c_dc, &total_dc);
        s_zc = bench_zerocopy_hot(P, header_pool, iters, c_zc, &total_zc);

        mbuf_free(P);
        mempool_destroy(payload_pool);
        mempool_destroy(header_pool);
    } else {
        const int ring_n = STREAM_RING_SIZE;
        /* Payload pool must hold ring + deepcopy scratch (2 outstanding). */
        Mempool *payload_pool = mempool_create((uint32_t)(ring_n + 1024), 0);
        Mempool *header_pool = mempool_create(8192, 1);
        if (!payload_pool || !header_pool) {
            fprintf(stderr, "pool create failed\n");
            return 1;
        }
        Mbuf **ring = (Mbuf **)malloc((size_t)ring_n * sizeof(Mbuf *));
        if (!ring) {
            fprintf(stderr, "ring malloc failed\n");
            return 1;
        }
        for (int i = 0; i < ring_n; i++) {
            ring[i] = mbuf_alloc(payload_pool);
            if (!ring[i]) {
                fprintf(stderr, "ring alloc failed at %d\n", i);
                return 1;
            }
            fill_payload(ring[i], i & 0xFF);
        }
        /* Warmup over a subset. */
        for (int i = 0; i < 1000; i++) {
            Mbuf *o1 = NULL, *o2 = NULL;
            duplicate_deepcopy(ring[i % ring_n], &o1, &o2, payload_pool);
            if (o1)
                mbuf_free(o1);
            if (o2)
                mbuf_free(o2);
            Mbuf *h1 = NULL, *h2 = NULL;
            duplicate_zerocopy(ring[i % ring_n], header_pool, &h1, &h2);
            if (h1)
                mbuf_free(h1);
            if (h2)
                mbuf_free(h2);
        }

        s_dc = bench_deepcopy_stream(ring, ring_n, payload_pool, iters, c_dc, &total_dc);
        s_zc = bench_zerocopy_stream(ring, ring_n, header_pool, iters, c_zc, &total_zc);

        for (int i = 0; i < ring_n; i++)
            mbuf_free(ring[i]);
        free(ring);
        mempool_destroy(payload_pool);
        mempool_destroy(header_pool);
    }

    printf("mode=%s iters=%d payload=800\n", mode, iters);
    printf("%-10s %10s %10s %10s %10s %10s %10s %12s\n",
           "method", "avg_cyc", "avg_ns", "mpps", "p50", "p95", "p99", "bytes_copy");
    printf("%-10s %10.1f %10.1f %10.3f %10llu %10llu %10llu %12d\n",
           "deepcopy", s_dc.avg_cycles, s_dc.avg_ns, s_dc.mpps,
           (unsigned long long)s_dc.p50, (unsigned long long)s_dc.p95,
           (unsigned long long)s_dc.p99, 1700);
    printf("%-10s %10.1f %10.1f %10.3f %10llu %10llu %10llu %12d\n",
           "zerocopy", s_zc.avg_cycles, s_zc.avg_ns, s_zc.mpps,
           (unsigned long long)s_zc.p50, (unsigned long long)s_zc.p95,
           (unsigned long long)s_zc.p99, 100);
    if (s_dc.avg_cycles > 0) {
        printf("speedup (cycles): %.2fx  (ns): %.2fx\n",
               s_dc.avg_cycles / (s_zc.avg_cycles > 0 ? s_zc.avg_cycles : 1),
               s_dc.avg_ns / (s_zc.avg_ns > 0 ? s_zc.avg_ns : 1));
    }

    /* Append to bench/results.csv (create header if missing/empty). */
    const char *csv_path = "bench/results.csv";
    FILE *f = fopen(csv_path, "r");
    int need_header = 1;
    if (f) {
        char buf[256];
        if (fgets(buf, sizeof(buf), f) != NULL)
            need_header = 0;
        fclose(f);
    }
    f = fopen(csv_path, "a");
    if (!f) {
        fprintf(stderr, "warning: cannot open %s for writing\n", csv_path);
    } else {
        if (need_header) {
            fprintf(f, "mode,method,iters,avg_cycles,avg_ns,mpps,p50_cycles,p95_cycles,p99_cycles,bytes_copied\n");
        }
        fprintf(f, "%s,deepcopy,%d,%.1f,%.1f,%.3f,%llu,%llu,%llu,1700\n",
                mode, iters, s_dc.avg_cycles, s_dc.avg_ns, s_dc.mpps,
                (unsigned long long)s_dc.p50, (unsigned long long)s_dc.p95,
                (unsigned long long)s_dc.p99);
        fprintf(f, "%s,zerocopy,%d,%.1f,%.1f,%.3f,%llu,%llu,%llu,100\n",
                mode, iters, s_zc.avg_cycles, s_zc.avg_ns, s_zc.mpps,
                (unsigned long long)s_zc.p50, (unsigned long long)s_zc.p95,
                (unsigned long long)s_zc.p99);
        fclose(f);
        printf("wrote %s\n", csv_path);
    }

    free(c_dc);
    free(c_zc);
    return 0;
}

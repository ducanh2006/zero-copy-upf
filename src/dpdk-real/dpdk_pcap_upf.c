/* dpdk_pcap_upf - PCAP replay -> zero-copy chained-mbuf duplicate -> TX.
 *
 * Modes (selected by vdev plumbing + --mode label):
 *   capture: RX net_pcap (infinite_rx=1) -> duplicate -> TX net_pcap files
 *            (small N, Wireshark evidence).
 *   perf:    RX net_pcap (infinite_rx=1) -> duplicate -> TX net_null sinks
 *            (line-rate Mpps, no disk I/O), stats -> bench/results_pcap.csv.
 *
 * Build: cmake -B build -DUSE_DPDK=ON && cmake --build build --target dpdk_pcap_upf
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>

#ifdef USE_DPDK

#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_version.h>

#include "dpdk_chain.h"
#include "dpdk_init.h"
#include "outer_header.h"

#define BURST_SIZE 32

static volatile int force_quit = 0;

static void handle_signal(int sig)
{
    (void)sig;
    force_quit = 1;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Count packets in a libpcap file (pre-EAL, for mempool sizing).
 * Returns count, or 0 on error. Handles LE/BE magic. */
static uint32_t count_pcap_packets(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t hdr[24];
    uint32_t n = 0;

    if (!f) {
        fprintf(stderr, "cannot open pcap for counting: %s\n", path);
        return 0;
    }
    if (fread(hdr, 1, 24, f) != 24) {
        fclose(f);
        return 0;
    }
    int le = (hdr[0] == 0xD4);
    if (!le && hdr[0] != 0xA1) {
        fprintf(stderr, "bad pcap magic in %s\n", path);
        fclose(f);
        return 0;
    }
    for (;;) {
        uint8_t rh[16];
        uint32_t incl;
        if (fread(rh, 1, 16, f) != 16)
            break;
        if (le)
            memcpy(&incl, rh + 8, 4);
        else {
            incl = ((uint32_t)rh[8] << 24) | ((uint32_t)rh[9] << 16) |
                   ((uint32_t)rh[10] << 8) | rh[11];
        }
        if (fseek(f, (long)incl, SEEK_CUR) != 0)
            break;
        n++;
    }
    fclose(f);
    return n;
}

/* Append the 50B N3/N9 outer header for path 1/2 into a header mbuf. */
static int fill_outer_header_dpdk(struct rte_mbuf *h, int path_id)
{
    uint8_t hdr[OUTER_HEADER_LEN];
    char *dst;

    build_outer_header(path_id, hdr);
    dst = rte_pktmbuf_append(h, OUTER_HEADER_LEN);
    if (!dst)
        return -1;
    memcpy(dst, hdr, OUTER_HEADER_LEN);
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [EAL args] -- --mode capture|perf [--packets N] "
        "[--duration S] [--csv bench/results_pcap.csv]\n"
        "  EAL example (capture): --no-pci --file-prefix=upf_pcap "
        "-d /tmp/upf_dpdk_drivers -l 0-3\n"
        "    --vdev \"net_pcap0,rx_pcap=data/pcap/sample_5g_traffic.pcap,"
        "infinite_rx=1\"\n"
        "    --vdev \"net_pcap1,tx_pcap=data/pcap/out_path1_gnb1.pcap\"\n"
        "    --vdev \"net_pcap2,tx_pcap=data/pcap/out_path2_gnb2.pcap\"\n"
        "  EAL example (perf): same RX plus --vdev net_null0 --vdev net_null1\n"
        "    as TX ports 1,2 (no disk I/O).\n", prog);
}

int main(int argc, char **argv)
{
    const char *mode = NULL;
    uint64_t max_packets = 0;
    uint64_t duration_sec = 0;
    const char *csv_path = "bench/results_pcap.csv";
    const uint16_t port_rx = 0, port_tx1 = 1, port_tx2 = 2;
    int ret;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        fprintf(stderr, "rte_eal_init failed\n");
        return 1;
    }
    argc -= ret;
    argv += ret;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc)
            mode = argv[++i];
        else if (strcmp(argv[i], "--packets") == 0 && i + 1 < argc)
            max_packets = (uint64_t)atoll(argv[++i]);
        else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc)
            duration_sec = (uint64_t)atoll(argv[++i]);
        else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc)
            csv_path = argv[++i];
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage("dpdk_pcap_upf");
            return 0;
        } else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            usage("dpdk_pcap_upf");
            return 1;
        }
    }
    if (!mode || (strcmp(mode, "capture") != 0 && strcmp(mode, "perf") != 0)) {
        fprintf(stderr, "--mode capture|perf is required\n");
        usage("dpdk_pcap_upf");
        return 1;
    }

    /* Pool sizing: infinite_rx preloads the whole pcap, so the RX pool
     * must hold >= pcap count. Derive from the rx_pcap file if given,
     * else fall back to a safe default (sample file = 256 pkts). */
    uint32_t pcap_n = 256;
    const char *rx_pcap = getenv("UPF_RX_PCAP");
    if (rx_pcap) {
        uint32_t c = count_pcap_packets(rx_pcap);
        if (c > 0)
            pcap_n = c;
        else {
            fprintf(stderr, "could not count %s, aborting\n", rx_pcap);
            return 1;
        }
    }
    printf("pcap packets (for sizing): %u\n", pcap_n);

    uint32_t nb_rx_mbufs = pcap_n + BURST_SIZE * 4 + 1024;
    if (nb_rx_mbufs < 2048)
        nb_rx_mbufs = 2048;

    struct rte_mempool *rx_pool = upf_create_pool("upf_rx_pool", nb_rx_mbufs);
    struct rte_mempool *header_pool = upf_create_pool("upf_hdr_pool", 8192);
    if (!rx_pool || !header_pool)
        return 1;

    if (upf_port_init(port_rx, rx_pool, 1, 0) != 0)
        return 1;
    if (upf_port_init(port_tx1, NULL, 0, 1) != 0)
        return 1;
    if (upf_port_init(port_tx2, NULL, 0, 1) != 0)
        return 1;
    printf("ports ready: rx=%u tx1=%u tx2=%u mode=%s\n",
           port_rx, port_tx1, port_tx2, mode);

    uint64_t total_received = 0, total_tx = 0;
    uint64_t cyc_start = rte_rdtsc();
    uint64_t t_start = now_ns();

    while (!force_quit) {
        struct rte_mbuf *rx_pkts[BURST_SIZE];
        struct rte_mbuf *tx_pkts1[BURST_SIZE];
        struct rte_mbuf *tx_pkts2[BURST_SIZE];
        struct rte_mbuf *tx_P[BURST_SIZE];
        uint16_t nb_rx, valid_count = 0;

        if (max_packets > 0 && total_received >= max_packets)
            break;
        if (duration_sec > 0 &&
            (now_ns() - t_start) >= duration_sec * 1000000000ull)
            break;

        nb_rx = rte_eth_rx_burst(port_rx, 0, rx_pkts, BURST_SIZE);
        if (nb_rx == 0)
            continue;
        total_received += nb_rx;

        for (uint16_t i = 0; i < nb_rx; i++) {
            struct rte_mbuf *P = rx_pkts[i];
            struct rte_mbuf *h1 = rte_pktmbuf_alloc(header_pool);
            struct rte_mbuf *h2 = rte_pktmbuf_alloc(header_pool);

            if (!h1 || !h2) {
                if (h1)
                    rte_pktmbuf_free(h1);
                if (h2)
                    rte_pktmbuf_free(h2);
                rte_pktmbuf_free(P);
                continue;
            }
            if (fill_outer_header_dpdk(h1, 1) != 0 ||
                fill_outer_header_dpdk(h2, 2) != 0) {
                rte_pktmbuf_free(h1);
                rte_pktmbuf_free(h2);
                rte_pktmbuf_free(P);
                continue;
            }
            if (dpdk_zerocopy_duplicate(P, h1, h2) != 0) {
                /* Chain failed: h1 may already hold a ref on P
                 * (wrapper drops it on free). Reclaim everything
                 * including P to avoid leaking the RX mbuf. */
                rte_pktmbuf_free(h1);
                rte_pktmbuf_free(h2);
                rte_pktmbuf_free(P);
                continue;
            }
            tx_pkts1[valid_count] = h1;
            tx_pkts2[valid_count] = h2;
            tx_P[valid_count] = P;
            valid_count++;
        }

        if (valid_count > 0) {
            uint16_t nb_tx1 = rte_eth_tx_burst(port_tx1, 0, tx_pkts1,
                                               valid_count);
            uint16_t nb_tx2 = rte_eth_tx_burst(port_tx2, 0, tx_pkts2,
                                               valid_count);
            for (uint16_t j = nb_tx1; j < valid_count; j++)
                rte_pktmbuf_free(tx_pkts1[j]);
            for (uint16_t j = nb_tx2; j < valid_count; j++)
                rte_pktmbuf_free(tx_pkts2[j]);
            /* Drop our original RX reference on P (1->0 reclaim into the
             * RX pool). Safe any time after chaining: h1/h2 hold their own
             * refs, and TX-owned segments keep P alive via refcnt until
             * the PMD frees them. Without this, infinite_rx drains the RX
             * pool (each rx_burst consumes fresh mbufs) and RX stalls. */
            for (uint16_t j = 0; j < valid_count; j++)
                rte_pktmbuf_free(tx_P[j]);
            /* Per-path TX confirmations; wire traffic = tx1 + tx2. */
            total_tx += (uint64_t)nb_tx1 + (uint64_t)nb_tx2;
        }
    }

    uint64_t t_end = now_ns();
    uint64_t cyc_end = rte_rdtsc();
    double elapsed_s = (double)(t_end - t_start) / 1e9;
    double mpps = elapsed_s > 0 ? ((double)total_tx / elapsed_s) / 1e6 : 0.0;
    double avg_cyc = total_tx > 0 ?
        (double)(cyc_end - cyc_start) / (double)total_tx : 0.0;

    printf("mode=%s received=%llu tx_confirmed(both paths)=%llu "
           "time=%.3fs Mpps=%.3f avg_cycles/tx=%.1f\n",
           mode, (unsigned long long)total_received,
           (unsigned long long)total_tx, elapsed_s, mpps, avg_cyc);

    FILE *f = fopen(csv_path, "a");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", csv_path);
    } else {
        if (ftell(f) == 0)
            fprintf(f, "mode,received,tx_confirmed,seconds,mpps,"
                       "avg_cycles_per_tx,dpdk_version\n");
        fprintf(f, "%s,%llu,%llu,%.3f,%.3f,%.1f,%s\n",
                mode, (unsigned long long)total_received,
                (unsigned long long)total_tx, elapsed_s, mpps, avg_cyc,
                rte_version());
        fclose(f);
        printf("wrote %s\n", csv_path);
    }

    rte_eth_dev_stop(port_rx);
    rte_eth_dev_stop(port_tx1);
    rte_eth_dev_stop(port_tx2);
    rte_eth_dev_close(port_rx);
    rte_eth_dev_close(port_tx1);
    rte_eth_dev_close(port_tx2);
    rte_eal_cleanup();
    return 0;
}

#else /* !USE_DPDK */

#include <stdio.h>

int main(void)
{
    fprintf(stderr, "dpdk_pcap_upf requires -DUSE_DPDK=ON\n");
    return 1;
}

#endif

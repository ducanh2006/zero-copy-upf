#include "dpdk_chain.h"

#ifdef USE_DPDK

#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <stdio.h>

/* EAL init for laptops without a DPDK-bound NIC:
 * --no-pci + virtual PMD (net_ring / net_null).
 * Returns 0 on success, <0 on failure. Safe to call once. */
int dpdk_eal_init_no_pci(void)
{
    static int inited = 0;
    if (inited)
        return 0;

    char *argv[] = {
        (char *)"skyt_upf",
        (char *)"--no-pci",
        (char *)"--vdev=net_ring0",
        (char *)"--vdev=net_null0",
        NULL,
    };
    int argc = 4;
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        fprintf(stderr, "rte_eal_init(--no-pci + net_ring0) failed\n");
        return ret;
    }
    inited = 1;
    return 0;
}

/* True zero-copy duplicate with real DPDK APIs.
 * Mirrors src/zerocopy.c logic 1-1:
 *   refcnt +2 on P, then rte_pktmbuf_chain(h1,P) + rte_pktmbuf_chain(h2,P).
 * Caller must have allocated h1/h2 (e.g. rte_pktmbuf_alloc on a header pool)
 * and filled their 50B outer headers. P holds the 800B payload.
 * NOTE: For chained multi-segment TX the port must enable
 * RTE_ETH_TX_OFFLOAD_MULTI_SEGS (see docs/dpdk-deployment-guide.md). */
int dpdk_zerocopy_duplicate(struct rte_mbuf *P,
                            struct rte_mbuf *h1,
                            struct rte_mbuf *h2)
{
    if (!P || !h1 || !h2)
        return -1;

    rte_pktmbuf_refcnt_update(P, 1);
    if (rte_pktmbuf_chain(h1, P) != 0) {
        rte_pktmbuf_refcnt_update(P, -1);
        return -1;
    }

    rte_pktmbuf_refcnt_update(P, 1);
    if (rte_pktmbuf_chain(h2, P) != 0) {
        /* h1 already chained; caller will free h1 (drops one ref). */
        rte_pktmbuf_refcnt_update(P, -1);
        return -1;
    }

    return 0;
}

#else /* !USE_DPDK: fallback to high-fidelity C11 model */

#include "zerocopy.h"

size_t dpdk_fallback_zerocopy(Mbuf *P, Mempool *header_pool,
                              Mbuf **h1_out, Mbuf **h2_out)
{
    return duplicate_zerocopy(P, header_pool, h1_out, h2_out);
}

#endif

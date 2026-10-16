#ifndef DPDK_CHAIN_H
#define DPDK_CHAIN_H

/* Real-DPDK adapter (compile-check + API mapping) + fallback for OFF builds. */

#ifdef USE_DPDK
#include <rte_mbuf.h>

int dpdk_eal_init_no_pci(void);
int dpdk_zerocopy_duplicate(struct rte_mbuf *P,
                            struct rte_mbuf *h1,
                            struct rte_mbuf *h2);

#else /* fallback: high-fidelity model */

#include <stddef.h>
#include "mbuf_model.h"

size_t dpdk_fallback_zerocopy(Mbuf *P, Mempool *header_pool,
                              Mbuf **h1_out, Mbuf **h2_out);

#endif

#endif

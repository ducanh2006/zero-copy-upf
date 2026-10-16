#ifndef DPDK_INIT_H
#define DPDK_INIT_H

#include <stdint.h>

#ifdef USE_DPDK
#include <rte_mempool.h>

#define UPF_RX_DESC 1024
#define UPF_TX_DESC 1024
#define UPF_MBUF_DATA_ROOM 2048
#define UPF_MEMPOOL_CACHE 256

/* Create an mbuf pool with exactly nb_mbufs entries.
 * Returns NULL on failure (caller prints rte_errno). */
struct rte_mempool *upf_create_pool(const char *name, uint32_t nb_mbufs);

/* Configure + setup + start one port.
 * is_rx: setup 1 RX queue backed by rx_pool (required when is_rx).
 * is_tx: setup 1 TX queue. Returns 0 on success, <0 on failure. */
int upf_port_init(uint16_t port_id, struct rte_mempool *rx_pool,
                   int is_rx, int is_tx);

#endif /* USE_DPDK */
#endif

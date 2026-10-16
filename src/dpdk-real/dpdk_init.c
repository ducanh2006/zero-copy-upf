#include "dpdk_init.h"

#ifdef USE_DPDK

#include <stdio.h>
#include <string.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

struct rte_mempool *upf_create_pool(const char *name, uint32_t nb_mbufs)
{
    struct rte_mempool *mp = rte_pktmbuf_pool_create(
        name, nb_mbufs, UPF_MEMPOOL_CACHE, 0,
        UPF_MBUF_DATA_ROOM, rte_socket_id());
    if (!mp)
        fprintf(stderr, "rte_pktmbuf_pool_create(%s, %u) failed: %s\n",
                name, nb_mbufs, rte_strerror(rte_errno));
    return mp;
}

int upf_port_init(uint16_t port_id, struct rte_mempool *rx_pool,
                   int is_rx, int is_tx)
{
    struct rte_eth_conf conf;

    memset(&conf, 0, sizeof(conf));
    int ret = rte_eth_dev_configure(port_id, is_rx ? 1 : 0,
                                    is_tx ? 1 : 0, &conf);
    if (ret != 0) {
        fprintf(stderr, "rte_eth_dev_configure(port %u) failed: %s\n",
                port_id, rte_strerror(-ret));
        return ret;
    }
    if (is_rx) {
        if (!rx_pool) {
            fprintf(stderr, "port %u: RX requested but rx_pool is NULL\n",
                    port_id);
            return -1;
        }
        /* NOTE: with net_pcap infinite_rx=1, this call preloads the ENTIRE
         * pcap into an internal ring, so rx_pool must hold >= pcap packet
         * count mbufs or setup fails with -EINVAL. Size pools accordingly. */
        ret = rte_eth_rx_queue_setup(port_id, 0, UPF_RX_DESC,
                                     rte_eth_dev_socket_id(port_id),
                                     NULL, rx_pool);
        if (ret != 0) {
            fprintf(stderr, "rx_queue_setup(port %u) failed: %s\n",
                    port_id, rte_strerror(-ret));
            return ret;
        }
    }
    if (is_tx) {
        ret = rte_eth_tx_queue_setup(port_id, 0, UPF_TX_DESC,
                                     rte_eth_dev_socket_id(port_id), NULL);
        if (ret != 0) {
            fprintf(stderr, "tx_queue_setup(port %u) failed: %s\n",
                    port_id, rte_strerror(-ret));
            return ret;
        }
    }
    ret = rte_eth_dev_start(port_id);
    if (ret != 0) {
        fprintf(stderr, "rte_eth_dev_start(port %u) failed: %s\n",
                port_id, rte_strerror(-ret));
        return ret;
    }
    return 0;
}
#endif

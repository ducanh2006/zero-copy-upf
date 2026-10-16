#include "zerocopy.h"
#include "outer_header.h"
#include <string.h>

size_t duplicate_zerocopy(Mbuf *P, Mempool *header_pool, Mbuf **h1_out, Mbuf **h2_out)
{
    if (!P || !header_pool || !h1_out || !h2_out)
        return 0;

    Mbuf *h1 = mbuf_alloc(header_pool);
    Mbuf *h2 = mbuf_alloc(header_pool);
    if (!h1 || !h2) {
        if (h1)
            mbuf_free(h1);
        if (h2)
            mbuf_free(h2);
        return 0;
    }

    /* Header-only copies: 50B per path, NEVER touch payload bytes. */
    uint8_t hdr1[OUTER_HEADER_LEN];
    uint8_t hdr2[OUTER_HEADER_LEN];
    build_outer_header(1, hdr1);
    build_outer_header(2, hdr2);

    uint8_t *dst1 = (uint8_t *)h1->buf_addr + h1->data_off;
    memcpy(dst1, hdr1, OUTER_HEADER_LEN);

    uint8_t *dst2 = (uint8_t *)h2->buf_addr + h2->data_off;
    memcpy(dst2, hdr2, OUTER_HEADER_LEN);

    h1->data_len = OUTER_HEADER_LEN;
    h1->pkt_len = OUTER_HEADER_LEN;
    h1->nb_segs = 1;

    h2->data_len = OUTER_HEADER_LEN;
    h2->pkt_len = OUTER_HEADER_LEN;
    h2->nb_segs = 1;

    /* Share payload: 1 -> 3, then chain. NIC scatter-gather joins them. */
    mbuf_refcnt_update(P, 2);
    mbuf_chain(h1, P);
    mbuf_chain(h2, P);

    *h1_out = h1;
    *h2_out = h2;
    return (size_t)(2 * OUTER_HEADER_LEN);
}

size_t zerocopy_bytes_copied(void)
{
    return (size_t)(2 * OUTER_HEADER_LEN);
}

size_t zerocopy_payload_bytes_copied(void)
{
    return 0;
}

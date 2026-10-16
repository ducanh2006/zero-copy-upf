#include "deepcopy.h"
#include "outer_header.h"
#include <string.h>

size_t duplicate_deepcopy(Mbuf *payload, Mbuf **out1, Mbuf **out2, Mempool *pool)
{
    if (!payload || !out1 || !out2 || !pool)
        return 0;

    uint8_t *payload_data = (uint8_t *)payload->buf_addr + payload->data_off;
    uint16_t payload_len = payload->data_len;
    if (payload_len == 0)
        payload_len = OUTER_PAYLOAD_LEN;
    if (payload_len > MBUF_DEFAULT_BUF_SIZE - OUTER_HEADER_LEN)
        return 0;

    Mbuf *p1 = mbuf_alloc(pool);
    Mbuf *p2 = mbuf_alloc(pool);
    if (!p1 || !p2) {
        if (p1)
            mbuf_free(p1);
        if (p2)
            mbuf_free(p2);
        return 0;
    }

    uint8_t hdr1[OUTER_HEADER_LEN];
    uint8_t hdr2[OUTER_HEADER_LEN];
    build_outer_header(1, hdr1);
    build_outer_header(2, hdr2);

    uint8_t *dst1 = (uint8_t *)p1->buf_addr + p1->data_off;
    uint8_t *dst2 = (uint8_t *)p2->buf_addr + p2->data_off;

    /* Traditional deep-copy: header + full payload for each copy. */
    memcpy(dst1, hdr1, OUTER_HEADER_LEN);
    memcpy(dst1 + OUTER_HEADER_LEN, payload_data, payload_len);

    memcpy(dst2, hdr2, OUTER_HEADER_LEN);
    memcpy(dst2 + OUTER_HEADER_LEN, payload_data, payload_len);

    p1->data_len = (uint16_t)(OUTER_HEADER_LEN + payload_len);
    p1->pkt_len = p1->data_len;
    p1->nb_segs = 1;

    p2->data_len = (uint16_t)(OUTER_HEADER_LEN + payload_len);
    p2->pkt_len = p2->data_len;
    p2->nb_segs = 1;

    *out1 = p1;
    *out2 = p2;
    return (size_t)(2 * (OUTER_HEADER_LEN + payload_len));
}

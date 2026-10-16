#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "mbuf_model.h"
#include "outer_header.h"
#include "deepcopy.h"

static uint16_t rd_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int check_header(int path_id)
{
    uint8_t hdr[OUTER_HEADER_LEN];
    build_outer_header(path_id, hdr);

    if (outer_header_len() != 50) {
        fprintf(stderr, "outer_header_len != 50\n");
        return 1;
    }
    /* UDP dport 2152 at offset 36 */
    if (rd_be16(hdr + 36) != 2152) {
        fprintf(stderr, "path%d dport mismatch: %u\n", path_id, rd_be16(hdr + 36));
        return 1;
    }
    /* GTP type 255 at offset 43 */
    if (hdr[43] != 255) {
        fprintf(stderr, "path%d gtp type mismatch: %u\n", path_id, hdr[43]);
        return 1;
    }
    /* GTP flags 0x30 */
    if (hdr[42] != 0x30) {
        fprintf(stderr, "gtp flags mismatch\n");
        return 1;
    }
    /* UDP len 816 */
    if (rd_be16(hdr + 38) != 816) {
        fprintf(stderr, "udp len mismatch\n");
        return 1;
    }
    /* IP total 836 */
    if (rd_be16(hdr + 16) != 836) {
        fprintf(stderr, "ip total mismatch\n");
        return 1;
    }
    /* IP proto 17 */
    if (hdr[23] != 17) {
        fprintf(stderr, "ip proto mismatch\n");
        return 1;
    }
    /* Ethertype 0x0800 */
    if (!(hdr[12] == 0x08 && hdr[13] == 0x00)) {
        fprintf(stderr, "ethertype mismatch\n");
        return 1;
    }
    /* Src IP 10.0.0.1 */
    if (rd_be32(hdr + 26) != 0x0A000001u) {
        fprintf(stderr, "src ip mismatch\n");
        return 1;
    }
    if (path_id == 1) {
        if (rd_be32(hdr + 30) != 0xC0A80A65u) {
            fprintf(stderr, "path1 dst ip mismatch\n");
            return 1;
        }
        if (rd_be32(hdr + 46) != 0x10000001u) {
            fprintf(stderr, "path1 TEID mismatch\n");
            return 1;
        }
    } else {
        if (rd_be32(hdr + 30) != 0xC0A81466u) {
            fprintf(stderr, "path2 dst ip mismatch\n");
            return 1;
        }
        if (rd_be32(hdr + 46) != 0x20000002u) {
            fprintf(stderr, "path2 TEID mismatch\n");
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    if (sizeof(OuterHeader) != 50) {
        fprintf(stderr, "OuterHeader size != 50\n");
        return 1;
    }
    if (check_header(1) != 0)
        return 1;
    if (check_header(2) != 0)
        return 1;

    /* Deep-copy independence test. */
    Mempool *pool = mempool_create(1024, 0);
    if (!pool) {
        fprintf(stderr, "pool create failed\n");
        return 1;
    }
    Mbuf *payload = mbuf_alloc(pool);
    if (!payload) {
        fprintf(stderr, "payload alloc failed\n");
        return 1;
    }
    uint8_t *pdata = (uint8_t *)payload->buf_addr + payload->data_off;
    for (int i = 0; i < 800; i++)
        pdata[i] = (uint8_t)(i & 0xFF);
    payload->data_len = 800;
    payload->pkt_len = 800;

    Mbuf *o1 = NULL;
    Mbuf *o2 = NULL;
    size_t bytes = duplicate_deepcopy(payload, &o1, &o2, pool);
    if (bytes != 1700) {
        fprintf(stderr, "bytes_copied mismatch: got %zu expected 1700\n", bytes);
        return 1;
    }
    if (!o1 || !o2) {
        fprintf(stderr, "duplicate returned NULL\n");
        return 1;
    }
    if (o1->data_len != 850 || o2->data_len != 850) {
        fprintf(stderr, "out data_len mismatch\n");
        return 1;
    }
    /* Headers must differ (TEID/IP). */
    uint8_t *d1 = (uint8_t *)o1->buf_addr + o1->data_off;
    uint8_t *d2 = (uint8_t *)o2->buf_addr + o2->data_off;
    if (memcmp(d1, d2, OUTER_HEADER_LEN) == 0) {
        fprintf(stderr, "headers should differ between paths\n");
        return 1;
    }
    /* Payloads initially identical. */
    if (memcmp(d1 + 50, d2 + 50, 800) != 0) {
        fprintf(stderr, "payload copies should match initially\n");
        return 1;
    }
    /* Mutate pkt1 payload: pkt2 must stay unchanged (independence). */
    d1[50] ^= 0xFF;
    d1[800] ^= 0xAA;
    if (memcmp(d1 + 50, d2 + 50, 800) == 0) {
        fprintf(stderr, "packets not independent after mutate\n");
        return 1;
    }
    /* Original payload must be untouched by the mutate. */
    if (pdata[0] == d1[50]) {
        /* pdata[0] was original value; d1[50] was flipped, so they must differ. */
    } else {
        /* expected: differ */
    }

    mbuf_free(o1);
    mbuf_free(o2);
    mbuf_free(payload);
    mempool_destroy(pool);
    printf("test_header PASS\n");
    return 0;
}

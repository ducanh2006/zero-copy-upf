#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "mbuf_model.h"
#include "outer_header.h"
#include "zerocopy.h"

static uint16_t rc_of(Mbuf *m) { return atomic_load(&m->refcnt); }

#define CHECK(c, msg) do { \
    if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } \
} while (0)

int main(void)
{
    Mempool *payload_pool = mempool_create(1024, 0);
    Mempool *header_pool = mempool_create(1024, 1);
    CHECK(payload_pool && header_pool, "pool create");

    Mbuf *P = mbuf_alloc(payload_pool);
    CHECK(P != NULL, "payload alloc");
    uint8_t *pdata = (uint8_t *)P->buf_addr + P->data_off;
    for (int i = 0; i < 800; i++)
        pdata[i] = (uint8_t)((i * 31 + 7) & 0xFF);
    P->data_len = 800;
    P->pkt_len = 800;
    CHECK(rc_of(P) == 1, "P refcnt init 1");

    Mbuf *h1 = NULL;
    Mbuf *h2 = NULL;
    size_t bytes = duplicate_zerocopy(P, header_pool, &h1, &h2);
    CHECK(bytes == 100, "zerocopy bytes == 100");
    CHECK(zerocopy_bytes_copied() == 100, "helper 100");
    CHECK(zerocopy_payload_bytes_copied() == 0, "payload 0");
    CHECK(h1 && h2, "h1/h2 non-null");

    /* Ownership: P 1 -> 3, chained. */
    CHECK(rc_of(P) == 3, "P refcnt 3");
    CHECK(h1->next == P, "h1->next == P");
    CHECK(h2->next == P, "h2->next == P");
    CHECK(h1->nb_segs == 2 && h2->nb_segs == 2, "nb_segs 2");
    CHECK(h1->pkt_len == 850 && h2->pkt_len == 850, "pkt_len 850");
    CHECK(h1->data_len == 50 && h2->data_len == 50, "header data_len 50");

    /* Zero-copy proof: both chains share the SAME payload buffer (no copy). */
    uint8_t *pl_via_h1 = (uint8_t *)h1->next->buf_addr + h1->next->data_off;
    uint8_t *pl_via_h2 = (uint8_t *)h2->next->buf_addr + h2->next->data_off;
    CHECK(pl_via_h1 == pdata, "payload pointer shared (h1)");
    CHECK(pl_via_h2 == pdata, "payload pointer shared (h2)");
    CHECK(memcmp(pl_via_h1, pl_via_h2, 800) == 0, "shared payload identical");

    /* Headers must differ (TEID/IP per path). */
    uint8_t *hd1 = (uint8_t *)h1->buf_addr + h1->data_off;
    uint8_t *hd2 = (uint8_t *)h2->buf_addr + h2->data_off;
    CHECK(memcmp(hd1, hd2, OUTER_HEADER_LEN) != 0, "headers differ");

    /* Sequential free lifecycle. */
    mbuf_free(h1);
    CHECK(rc_of(P) == 2, "after free h1 P==2");
    mbuf_free(h2);
    CHECK(rc_of(P) == 1, "after free h2 P==1");
    mbuf_free(P);
    CHECK(rc_of(P) == 0, "after free P ==0");

    mempool_destroy(payload_pool);
    mempool_destroy(header_pool);
    printf("test_zerocopy PASS\n");
    return 0;
}

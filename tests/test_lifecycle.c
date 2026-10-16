#include <stdio.h>
#include <assert.h>
#include <stdatomic.h>
#include "mbuf_model.h"

static uint16_t rc_of(Mbuf *m)
{
    return atomic_load(&m->refcnt);
}

#define CHECK_RC(m, expected) do { \
    uint16_t _rc = rc_of(m); \
    if (_rc != (expected)) { \
        fprintf(stderr, "refcnt mismatch at %s:%d: got %u expected %u\n", \
                __FILE__, __LINE__, _rc, (unsigned)(expected)); \
        return 1; \
    } \
} while (0)

int main(void)
{
    Mempool *payload_pool = mempool_create(4096, 0);
    Mempool *header_pool = mempool_create(4096, 1);
    assert(payload_pool != NULL && header_pool != NULL);

    Mbuf *P = mbuf_alloc(payload_pool);
    assert(P != NULL);
    P->data_len = 800;
    P->pkt_len = 800;
    CHECK_RC(P, 1);
    assert(rc_of(P) == 1);

    /* Share payload with 2 headers: 1 -> 3. */
    mbuf_refcnt_update(P, 2);
    CHECK_RC(P, 3);
    assert(rc_of(P) == 3);

    Mbuf *h1 = mbuf_alloc(header_pool);
    Mbuf *h2 = mbuf_alloc(header_pool);
    if (h1 == NULL || h2 == NULL) { fprintf(stderr, "header alloc failed\n"); return 1; }
    assert(h1 != NULL && h2 != NULL);
    h1->data_len = 50; h1->pkt_len = 50;
    h2->data_len = 50; h2->pkt_len = 50;

    int cr1 = mbuf_chain(h1, P);
    int cr2 = mbuf_chain(h2, P);
    if (cr1 != 0 || cr2 != 0) { fprintf(stderr, "chain failed\n"); return 1; }
    assert(cr1 == 0 && cr2 == 0);
    if (h1->next != P || h2->next != P) { fprintf(stderr, "chain link wrong\n"); return 1; }
    assert(h1->next == P && h2->next == P);

    /* Free h1 -> P 3 -> 2 retained. */
    mbuf_free(h1);
    CHECK_RC(P, 2);
    assert(rc_of(P) == 2);
    assert(P->data_len == 800); /* payload retained, not reclaimed */

    /* Free h2 -> P 2 -> 1 retained. */
    mbuf_free(h2);
    CHECK_RC(P, 1);
    assert(rc_of(P) == 1);

    /* Free P -> 1 -> 0 reclaimed. */
    mbuf_free(P);
    CHECK_RC(P, 0);
    assert(rc_of(P) == 0);

    /* 1M alloc/free loop: no leak, pool stays fully usable. */
    for (int i = 0; i < 1000000; i++) {
        Mbuf *m = mbuf_alloc(payload_pool);
        if (m == NULL) {
            fprintf(stderr, "alloc failed at iter %d\n", i);
            return 1;
        }
        mbuf_free(m);
    }

    /* After the loop the pool must still offer full capacity. */
    const uint32_t cap = 4096;
    static Mbuf *tmp[4096];
    for (uint32_t i = 0; i < cap; i++) {
        tmp[i] = mbuf_alloc(payload_pool);
        if (tmp[i] == NULL) { fprintf(stderr, "post-loop alloc failed at %u\n", i); return 1; }
        assert(tmp[i] != NULL);
    }
    Mbuf *extra = mbuf_alloc(payload_pool);
    if (extra != NULL) { fprintf(stderr, "pool should be exhausted\n"); return 1; }
    assert(extra == NULL);
    for (uint32_t i = 0; i < cap; i++)
        mbuf_free(tmp[i]);

    mempool_destroy(payload_pool);
    mempool_destroy(header_pool);
    printf("test_lifecycle PASS\n");
    return 0;
}

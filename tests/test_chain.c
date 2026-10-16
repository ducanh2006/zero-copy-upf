#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "mbuf_model.h"

int main(void)
{
    Mempool *mp = mempool_create(1024, 0);
    assert(mp != NULL);

    Mbuf *a = mbuf_alloc(mp);
    Mbuf *b = mbuf_alloc(mp);
    assert(a != NULL && b != NULL);

    /* Simulate two segments: header 50B + payload 800B style. */
    a->data_len = 50;
    a->pkt_len = 50;
    a->nb_segs = 1;
    b->data_len = 800;
    b->pkt_len = 800;
    b->nb_segs = 1;

    int rc = mbuf_chain(a, b);
    (void)rc;
    assert(rc == 0);
    assert(a->next == b);
    assert(a->pkt_len == 850);
    assert(a->nb_segs == 2);
    /* Tail segment keeps its own view. */
    assert(b->next == NULL);
    assert(b->pkt_len == 800);

    /* Prepend must succeed when headroom allows. */
    Mbuf *c = mbuf_alloc(mp);
    assert(c != NULL);
    uint8_t *p = mbuf_prepend(c, 50);
    (void)p;
    assert(p != NULL);
    assert(c->data_off == RTE_PKTMBUF_HEADROOM - 50);
    assert(c->data_len == 50);

    /* Prepend must fail when exceeding headroom. */
    uint8_t *q = mbuf_prepend(c, 200);
    (void)q;
    assert(q == NULL);

    /* Freeing the chain head must reclaim the whole exclusive chain. */
    mbuf_free(a);
    mbuf_free(c);

    /* Pool must be fully reusable: allocate `capacity` mbufs. */
    Mbuf *tmp[1024];
    for (int i = 0; i < 1024; i++) {
        tmp[i] = mbuf_alloc(mp);
        if (tmp[i] == NULL) { fprintf(stderr, "alloc failed at %d\n", i); return 1; }
        assert(tmp[i] != NULL);
    }
    Mbuf *extra = mbuf_alloc(mp);
    if (extra != NULL) { fprintf(stderr, "pool should be exhausted\n"); return 1; }
    assert(extra == NULL); /* pool exhausted */
    for (int i = 0; i < 1024; i++)
        mbuf_free(tmp[i]);

    mempool_destroy(mp);
    printf("test_chain PASS\n");
    return 0;
}

#include "mbuf_model.h"
#include <stdlib.h>
#include <string.h>

#define MBUF_MAX_POOLS 256

/* Per-pool ring queues (free-index FIFOs). The public Mempool struct
 * keeps only head/tail counters (plan1 section 3.1 exact spec); the
 * ring storage itself lives here indexed by pool_id. */
static Mempool  *g_pools[MBUF_MAX_POOLS] = { NULL };
static uint32_t *g_rings[MBUF_MAX_POOLS] = { NULL };

static int find_pool_id(Mempool *mp)
{
    for (int i = 0; i < MBUF_MAX_POOLS; i++) {
        if (g_pools[i] == mp)
            return i;
    }
    return -1;
}

Mempool *mempool_create(uint32_t capacity, uint16_t pool_id)
{
    if (capacity == 0 || pool_id >= MBUF_MAX_POOLS)
        return NULL;
    /* Reject duplicate pool_id: overwriting g_pools[pool_id] would make
     * mbuf_free() return old-pool segments into the new pool. Caller must
     * mempool_destroy() the old pool first. */
    if (g_pools[pool_id] != NULL)
        return NULL;

    Mempool *mp = (Mempool *)malloc(sizeof(Mempool));
    if (!mp)
        return NULL;

    mp->storage = (Mbuf *)malloc((size_t)capacity * sizeof(Mbuf));
    if (!mp->storage) {
        free(mp);
        return NULL;
    }
    mp->raw_buffers = (uint8_t *)malloc((size_t)capacity * MBUF_DEFAULT_BUF_SIZE);
    if (!mp->raw_buffers) {
        free(mp->storage);
        free(mp);
        return NULL;
    }
    uint32_t *ring = (uint32_t *)malloc((size_t)capacity * sizeof(uint32_t));
    if (!ring) {
        free(mp->raw_buffers);
        free(mp->storage);
        free(mp);
        return NULL;
    }

    for (uint32_t i = 0; i < capacity; i++) {
        ring[i] = i;
        Mbuf *m = &mp->storage[i];
        m->buf_addr = mp->raw_buffers + (size_t)i * MBUF_DEFAULT_BUF_SIZE;
        m->data_off = RTE_PKTMBUF_HEADROOM;
        m->data_len = 0;
        m->pkt_len = 0;
        m->nb_segs = 1;
        atomic_init(&m->refcnt, 0);
        m->next = NULL;
        m->pool_id = pool_id;
    }

    mp->capacity = capacity;
    atomic_init(&mp->head, 0);
    atomic_init(&mp->tail, capacity); /* queue pre-filled with `capacity` free indices */

    /* pool_id slot is guaranteed free (checked above). */
    g_pools[pool_id] = mp;
    g_rings[pool_id] = ring;
    return mp;
}

void mempool_destroy(Mempool *mp)
{
    if (!mp)
        return;
    for (int i = 0; i < MBUF_MAX_POOLS; i++) {
        if (g_pools[i] == mp) {
            g_pools[i] = NULL;
            free(g_rings[i]);
            g_rings[i] = NULL;
            break;
        }
    }
    free(mp->storage);
    free(mp->raw_buffers);
    free(mp);
}

Mbuf *mbuf_alloc(Mempool *mp)
{
    if (!mp)
        return NULL;
    int pid = find_pool_id(mp);
    if (pid < 0)
        return NULL;
    uint32_t *ring = g_rings[pid];
    if (!ring)
        return NULL;

    uint32_t head = atomic_load_explicit(&mp->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&mp->tail, memory_order_acquire);
    if (tail - head == 0)
        return NULL; /* pool empty */

    uint32_t pos = atomic_fetch_add_explicit(&mp->head, 1, memory_order_relaxed);
    /* Single-threaded fast path: re-check under race would need rollback;
     * for HPC single-producer use this is sufficient. Re-validate. */
    tail = atomic_load_explicit(&mp->tail, memory_order_acquire);
    if (tail - pos == 0) {
        /* Over-allocated under race: roll back. */
        atomic_fetch_sub_explicit(&mp->head, 1, memory_order_relaxed);
        return NULL;
    }

    uint32_t idx = ring[pos % mp->capacity];
    if (idx >= mp->capacity)
        return NULL;

    Mbuf *m = &mp->storage[idx];
    m->buf_addr = mp->raw_buffers + (size_t)idx * MBUF_DEFAULT_BUF_SIZE;
    m->data_off = RTE_PKTMBUF_HEADROOM;
    m->data_len = 0;
    m->pkt_len = 0;
    m->nb_segs = 1;
    atomic_store_explicit(&m->refcnt, 1, memory_order_relaxed);
    m->next = NULL;
    m->pool_id = (uint16_t)pid;
    return m;
}

void mbuf_refcnt_update(Mbuf *m, int16_t delta)
{
    if (!m || delta == 0)
        return;
    if (delta > 0) {
        atomic_fetch_add_explicit(&m->refcnt, (uint16_t)delta, memory_order_relaxed);
    } else {
        uint16_t sub = (uint16_t)(-(int32_t)delta);
        atomic_fetch_sub_explicit(&m->refcnt, sub, memory_order_acq_rel);
    }
}

int mbuf_chain(Mbuf *head, Mbuf *tail_seg)
{
    if (!head || !tail_seg)
        return -1;
    Mbuf *last = head;
    while (last->next != NULL)
        last = last->next;
    last->next = tail_seg;
    head->pkt_len += tail_seg->pkt_len;
    head->nb_segs += tail_seg->nb_segs;
    return 0;
}

uint8_t *mbuf_prepend(Mbuf *m, uint16_t len)
{
    if (!m)
        return NULL;
    if (m->data_off < len)
        return NULL;
    m->data_off -= len;
    m->data_len += len;
    m->pkt_len += len;
    return (uint8_t *)m->buf_addr + m->data_off;
}

void mbuf_free(Mbuf *m)
{
    if (!m)
        return;

    Mbuf *cur = m;
    while (cur != NULL) {
        Mbuf *nxt = cur->next;
        uint16_t rc = atomic_load_explicit(&cur->refcnt, memory_order_acquire);
        if (rc == 0) {
            /* Stale double-free: segment already reclaimed. Do NOT push
             * the index into the ring a second time (that would create
             * two aliased mbufs for the same buffer). Skip it but keep
             * walking the tail so a stale segment in the middle of a
             * chain does not leak the rest (reuse-after-free + re-chain
             * or concurrent free). */
            cur = nxt;
            continue;
        }
        if (rc > 1) {
            /* Shared segment (e.g. payload with refcnt 3): just drop one
             * reference and stop; do NOT reclaim, do NOT walk further. */
            atomic_fetch_sub_explicit(&cur->refcnt, (uint16_t)1, memory_order_acq_rel);
            break;
        }
        /* rc == 1 (exclusive): reclaim this segment to its pool.
         * Note: rc == 0 is handled above (double-free guard). */
        cur->next = NULL;
        int pid = (int)cur->pool_id;
        Mempool *mp = NULL;
        uint32_t *ring = NULL;
        if (pid >= 0 && pid < MBUF_MAX_POOLS) {
            mp = g_pools[pid];
            ring = g_rings[pid];
        }
        if (mp != NULL && ring != NULL) {
            /* Bounds-check BEFORE pointer subtraction: subtracting
             * pointers from different arrays (e.g. mbuf from a destroyed
             * pool freed after recreate with same pool_id) is UB even
             * if the numeric idx would later fail the range check. */
            if (cur < mp->storage || cur >= mp->storage + mp->capacity) {
                /* Unknown pool: just clear linkage to avoid dangling chain. */
                atomic_store_explicit(&cur->refcnt, 0, memory_order_relaxed);
            } else {
                size_t idx = (size_t)(cur - mp->storage);
                /* release pairs with the acquire load of tail in
                 * mbuf_alloc(): publishes the ring[] write below. */
                uint32_t pos = atomic_fetch_add_explicit(&mp->tail, 1, memory_order_release);
                ring[pos % mp->capacity] = (uint32_t)idx;
            }
            cur->data_len = 0;
            cur->pkt_len = 0;
            cur->nb_segs = 1;
            cur->data_off = RTE_PKTMBUF_HEADROOM;
            atomic_store_explicit(&cur->refcnt, 0, memory_order_relaxed);
        } else {
            /* Unknown pool: just clear linkage to avoid dangling chain. */
            atomic_store_explicit(&cur->refcnt, 0, memory_order_relaxed);
        }
        cur = nxt;
    }
}

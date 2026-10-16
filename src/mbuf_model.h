#ifndef MBUF_MODEL_H
#define MBUF_MODEL_H

#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>

#define RTE_PKTMBUF_HEADROOM 128
#define MBUF_DEFAULT_BUF_SIZE 2048

typedef struct Mbuf {
    void *buf_addr;              // Con tro toi vung dem vat ly (buffer raw)
    uint16_t data_off;          // Headroom offset (mac dinh RTE_PKTMBUF_HEADROOM)
    uint16_t data_len;          // Do dai du lieu co hieu luc trong segment nay
    uint32_t pkt_len;           // Tong do dai toan bo goi tin (toan chuoi segments)
    uint16_t nb_segs;           // So luong segments cau thanh goi tin
    _Atomic uint16_t refcnt;    // Reference count doc lap cua chinh mbuf nay
    struct Mbuf *next;          // Con tro tro toi segment tiep theo (NULL neu la segment cuoi)
    uint16_t pool_id;           // Phan loai: 0 = Payload Pool, 1 = Header Pool
} Mbuf;

// Ring Mempool tinh O(1)
typedef struct Mempool {
    Mbuf *storage;              // Mang tinh chua cac mbuf
    uint8_t *raw_buffers;       // Mang bo dem du lieu lien tuc
    uint32_t capacity;          // Suc chua toi da
    _Atomic uint32_t head;      // Vi tri lay mbuf (alloc)
    _Atomic uint32_t tail;      // Vi tri tra mbuf (free)
} Mempool;

Mempool* mempool_create(uint32_t capacity, uint16_t pool_id);
void     mempool_destroy(Mempool *mp);

Mbuf*    mbuf_alloc(Mempool *mp);
void     mbuf_free(Mbuf *m);
void     mbuf_refcnt_update(Mbuf *m, int16_t delta);
int      mbuf_chain(Mbuf *head, Mbuf *tail); // Ghep tail vao cuoi chuoi head
uint8_t* mbuf_prepend(Mbuf *m, uint16_t len); // Them header neu du headroom

#endif

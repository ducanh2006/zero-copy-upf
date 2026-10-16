#ifndef ZEROCOPY_H
#define ZEROCOPY_H

#include <stddef.h>
#include "mbuf_model.h"

/* Core zero-copy primitive: allocate 2 header mbufs, write ONLY the 50B
 * outer header into each, bump payload refcnt by 2 and chain.
 * Returns header bytes copied (100) on success, 0 on failure. */
size_t duplicate_zerocopy(Mbuf *P, Mempool *header_pool, Mbuf **h1_out, Mbuf **h2_out);

/* Accounting helpers for benchmark/docs. */
size_t zerocopy_bytes_copied(void);          /* 2 * 50 = 100 */
size_t zerocopy_payload_bytes_copied(void);  /* 0 */

#endif

#ifndef DEEPCOPY_H
#define DEEPCOPY_H

#include <stddef.h>
#include "mbuf_model.h"

/* Traditional baseline: allocate 2 new mbufs (850B each) and memcpy
 * both the 50B outer header and the 800B payload.
 * Returns total bytes copied (1700) on success, 0 on failure. */
size_t duplicate_deepcopy(Mbuf *payload, Mbuf **out1, Mbuf **out2, Mempool *pool);

#endif

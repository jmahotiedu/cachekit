#ifndef CK_EVICTION_H
#define CK_EVICTION_H

#include "store.h"

#define CK_EVICTION_SAMPLES 5

/* evict one key using approximate LRU via random sampling.
 * returns 1 if a key was evicted, 0 if nothing to evict. */
int eviction_run(store_t *s);

/* check if memory limit is exceeded and evict as needed.
 * returns number of keys evicted. */
int eviction_check(store_t *s);

#endif

#include "eviction.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int eviction_run(store_t *s) {
    if (ht_count(s->data) == 0) return 0;

    const char *victim = NULL;
    int64_t oldest_access = INT64_MAX;

    /* sample random keys and pick the least recently accessed */
    for (int i = 0; i < CK_EVICTION_SAMPLES; i++) {
        const char *key;
        if (!ht_random_key(s->data, &key)) break;

        store_entry_t *e = (store_entry_t *)ht_get(s->data, key);
        if (!e) continue;

        if (e->last_access < oldest_access) {
            oldest_access = e->last_access;
            victim = key;
        }
    }

    if (!victim) return 0;

    char *key_copy = ck_strdup(victim);
    ck_log(CK_LOG_DEBUG, "evicting key: %s", key_copy);
    ht_delete(s->data, key_copy);
    free(key_copy);
    return 1;
}

int eviction_check(store_t *s) {
    if (s->maxmemory == 0) return 0;

    int evicted = 0;
    while (ck_mem_used() > s->maxmemory && ht_count(s->data) > 0) {
        if (!eviction_run(s)) break;
        evicted++;
    }
    return evicted;
}

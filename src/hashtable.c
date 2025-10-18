#include "hashtable.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

#define HT_LOAD_GROW  0.70
#define HT_LOAD_SHRINK 0.10
#define HT_MIN_CAP    16

/* FNV-1a hash */
static uint32_t hash_key(const char *key) {
    uint32_t h = 2166136261u;
    for (const char *p = key; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    return h;
}

static size_t next_power_of_two(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

hashtable_t *ht_create(size_t initial_cap, void (*free_value)(void *)) {
    hashtable_t *ht = ck_malloc(sizeof(hashtable_t));
    if (initial_cap < HT_MIN_CAP) initial_cap = HT_MIN_CAP;
    initial_cap = next_power_of_two(initial_cap);

    ht->entries = ck_calloc(initial_cap, sizeof(ht_entry_t));
    ht->capacity = initial_cap;
    ht->count = 0;
    ht->free_value = free_value;

    /* mark all slots empty */
    for (size_t i = 0; i < initial_cap; i++) {
        ht->entries[i].psl = -1;
    }

    return ht;
}

void ht_destroy(hashtable_t *ht) {
    if (!ht) return;
    for (size_t i = 0; i < ht->capacity; i++) {
        if (ht->entries[i].psl >= 0) {
            free(ht->entries[i].key);
            if (ht->free_value && ht->entries[i].value) {
                ht->free_value(ht->entries[i].value);
            }
        }
    }
    free(ht->entries);
    free(ht);
}

static int ht_resize(hashtable_t *ht, size_t new_cap) {
    if (new_cap < HT_MIN_CAP) new_cap = HT_MIN_CAP;

    ht_entry_t *old_entries = ht->entries;
    size_t old_cap = ht->capacity;

    ht->entries = ck_calloc(new_cap, sizeof(ht_entry_t));
    ht->capacity = new_cap;
    ht->count = 0;

    for (size_t i = 0; i < new_cap; i++) {
        ht->entries[i].psl = -1;
    }

    /* reinsert all existing entries */
    for (size_t i = 0; i < old_cap; i++) {
        if (old_entries[i].psl >= 0) {
            ht_set(ht, old_entries[i].key, old_entries[i].value);
            free(old_entries[i].key);
        }
    }

    free(old_entries);
    return 0;
}

int ht_set(hashtable_t *ht, const char *key, void *value) {
    /* grow if load factor exceeded */
    if ((double)(ht->count + 1) / ht->capacity > HT_LOAD_GROW) {
        ht_resize(ht, ht->capacity * 2);
    }

    uint32_t h = hash_key(key);
    size_t mask = ht->capacity - 1;
    size_t idx = h & mask;
    int32_t psl = 0;

    ht_entry_t incoming;
    incoming.key = ck_strdup(key);
    incoming.value = value;
    incoming.hash = h;
    incoming.psl = 0;

    while (1) {
        ht_entry_t *slot = &ht->entries[idx];

        if (slot->psl < 0) {
            /* empty slot */
            incoming.psl = psl;
            *slot = incoming;
            ht->count++;
            return 1; /* new key */
        }

        /* key already exists - update */
        if (slot->hash == h && strcmp(slot->key, incoming.key) == 0) {
            if (ht->free_value && slot->value) {
                ht->free_value(slot->value);
            }
            slot->value = incoming.value;
            free(incoming.key);
            return 0; /* existing key updated */
        }

        /* Robin Hood: steal from rich slots */
        if (psl > slot->psl) {
            incoming.psl = psl;
            ht_entry_t tmp = *slot;
            *slot = incoming;
            incoming = tmp;
            psl = incoming.psl;
        }

        psl++;
        idx = (idx + 1) & mask;
    }
}

void *ht_get(hashtable_t *ht, const char *key) {
    uint32_t h = hash_key(key);
    size_t mask = ht->capacity - 1;
    size_t idx = h & mask;
    int32_t psl = 0;

    while (1) {
        ht_entry_t *slot = &ht->entries[idx];

        if (slot->psl < 0 || psl > slot->psl) {
            return NULL;
        }

        if (slot->hash == h && strcmp(slot->key, key) == 0) {
            return slot->value;
        }

        psl++;
        idx = (idx + 1) & mask;
    }
}

int ht_delete(hashtable_t *ht, const char *key) {
    uint32_t h = hash_key(key);
    size_t mask = ht->capacity - 1;
    size_t idx = h & mask;
    int32_t psl = 0;

    while (1) {
        ht_entry_t *slot = &ht->entries[idx];

        if (slot->psl < 0 || psl > slot->psl) {
            return 0;
        }

        if (slot->hash == h && strcmp(slot->key, key) == 0) {
            free(slot->key);
            if (ht->free_value && slot->value) {
                ht->free_value(slot->value);
            }
            slot->key = NULL;
            slot->value = NULL;
            slot->psl = -1;

            /* backward shift deletion to maintain Robin Hood invariant */
            size_t prev = idx;
            idx = (idx + 1) & mask;
            while (ht->entries[idx].psl > 0) {
                ht->entries[prev] = ht->entries[idx];
                ht->entries[prev].psl--;
                ht->entries[idx].psl = -1;
                ht->entries[idx].key = NULL;
                ht->entries[idx].value = NULL;
                prev = idx;
                idx = (idx + 1) & mask;
            }

            ht->count--;

            /* shrink if load factor too low */
            if (ht->capacity > HT_MIN_CAP &&
                (double)ht->count / ht->capacity < HT_LOAD_SHRINK) {
                ht_resize(ht, ht->capacity / 2);
            }

            return 1;
        }

        psl++;
        idx = (idx + 1) & mask;
    }
}

int ht_exists(hashtable_t *ht, const char *key) {
    return ht_get(ht, key) != NULL;
}

size_t ht_count(hashtable_t *ht) {
    return ht->count;
}

size_t ht_capacity(hashtable_t *ht) {
    return ht->capacity;
}

void ht_iter_init(ht_iter_t *iter, hashtable_t *ht) {
    iter->ht = ht;
    iter->index = 0;
}

int ht_iter_next(ht_iter_t *iter, const char **key, void **value) {
    while (iter->index < iter->ht->capacity) {
        ht_entry_t *e = &iter->ht->entries[iter->index];
        iter->index++;
        if (e->psl >= 0) {
            if (key) *key = e->key;
            if (value) *value = e->value;
            return 1;
        }
    }
    return 0;
}

int ht_random_key(hashtable_t *ht, const char **key) {
    if (ht->count == 0) return 0;

    size_t start = (size_t)rand() % ht->capacity;
    size_t idx = start;
    do {
        if (ht->entries[idx].psl >= 0) {
            *key = ht->entries[idx].key;
            return 1;
        }
        idx = (idx + 1) % ht->capacity;
    } while (idx != start);

    return 0;
}

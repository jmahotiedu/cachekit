#ifndef CK_HASHTABLE_H
#define CK_HASHTABLE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *key;
    void *value;
    uint32_t hash;
    /* probe distance from ideal slot (Robin Hood) */
    int32_t psl;
} ht_entry_t;

typedef struct {
    ht_entry_t *entries;
    size_t capacity;
    size_t count;
    void (*free_value)(void *);
} hashtable_t;

typedef struct {
    hashtable_t *ht;
    size_t index;
} ht_iter_t;

hashtable_t *ht_create(size_t initial_cap, void (*free_value)(void *));
void ht_destroy(hashtable_t *ht);

int ht_set(hashtable_t *ht, const char *key, void *value);
void *ht_get(hashtable_t *ht, const char *key);
int ht_delete(hashtable_t *ht, const char *key);
int ht_exists(hashtable_t *ht, const char *key);

size_t ht_count(hashtable_t *ht);
size_t ht_capacity(hashtable_t *ht);

/* iterator */
void ht_iter_init(ht_iter_t *iter, hashtable_t *ht);
int ht_iter_next(ht_iter_t *iter, const char **key, void **value);

/* get a random occupied key, for sampling */
int ht_random_key(hashtable_t *ht, const char **key);

#endif

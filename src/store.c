#define _POSIX_C_SOURCE 200112L
#include "store.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void free_entry(void *ptr) {
    store_entry_t *e = (store_entry_t *)ptr;
    if (!e) return;

    ck_mem_track_free(e->mem_usage);

    switch (e->type) {
        case CK_STRING:
            free(e->str);
            break;
        case CK_INT:
            break;
        case CK_LIST:
            list_destroy(e->list);
            break;
        case CK_HASH:
            ht_destroy(e->hash);
            break;
    }
    free(e);
}

store_t *store_create(void) {
    store_t *s = ck_malloc(sizeof(store_t));
    s->data = ht_create(64, free_entry);
    s->maxmemory = 0;
    return s;
}

void store_destroy(store_t *store) {
    if (!store) return;
    ht_destroy(store->data);
    free(store);
}

int store_is_expired(store_entry_t *e) {
    if (!e || e->expire_at == 0) return 0;
    return now_ms() >= e->expire_at;
}

/* lazy expiration: check and delete if expired, return NULL if so */
static store_entry_t *check_expiry(store_t *s, const char *key) {
    store_entry_t *e = (store_entry_t *)ht_get(s->data, key);
    if (!e) return NULL;

    if (store_is_expired(e)) {
        ht_delete(s->data, key);
        return NULL;
    }

    e->last_access = now_ms();
    return e;
}

int store_set(store_t *s, const char *key, const char *value) {
    store_entry_t *e = ck_malloc(sizeof(store_entry_t));
    e->type = CK_STRING;
    e->str = ck_strdup(value);
    e->expire_at = 0;
    e->last_access = now_ms();
    e->mem_usage = sizeof(store_entry_t) + strlen(value) + 1 + strlen(key) + 1;

    ck_mem_track_alloc(e->mem_usage);
    ht_set(s->data, key, e);
    return 0;
}

int store_set_int(store_t *s, const char *key, int64_t value) {
    store_entry_t *e = ck_malloc(sizeof(store_entry_t));
    e->type = CK_INT;
    e->integer = value;
    e->expire_at = 0;
    e->last_access = now_ms();
    e->mem_usage = sizeof(store_entry_t) + strlen(key) + 1;

    ck_mem_track_alloc(e->mem_usage);
    ht_set(s->data, key, e);
    return 0;
}

const char *store_get(store_t *s, const char *key) {
    store_entry_t *e = check_expiry(s, key);
    if (!e) return NULL;
    if (e->type != CK_STRING) return NULL;
    return e->str;
}

int store_get_int(store_t *s, const char *key, int64_t *out) {
    store_entry_t *e = check_expiry(s, key);
    if (!e) return -1;

    if (e->type == CK_INT) {
        *out = e->integer;
        return 0;
    }
    if (e->type == CK_STRING) {
        return ck_str_to_int64(e->str, out);
    }
    return -1;
}

store_entry_t *store_get_entry(store_t *s, const char *key) {
    return check_expiry(s, key);
}

int store_del(store_t *s, const char *key) {
    return ht_delete(s->data, key);
}

int store_exists(store_t *s, const char *key) {
    store_entry_t *e = check_expiry(s, key);
    return e != NULL;
}

ck_type_t store_type(store_t *s, const char *key) {
    store_entry_t *e = check_expiry(s, key);
    if (!e) return CK_STRING; /* default, caller should check exists first */
    return e->type;
}

int store_expire(store_t *s, const char *key, int64_t seconds) {
    store_entry_t *e = check_expiry(s, key);
    if (!e) return 0;
    e->expire_at = now_ms() + seconds * 1000;
    return 1;
}

int64_t store_ttl(store_t *s, const char *key) {
    store_entry_t *e = (store_entry_t *)ht_get(s->data, key);
    if (!e) return -2; /* key not found */

    if (store_is_expired(e)) {
        ht_delete(s->data, key);
        return -2;
    }

    if (e->expire_at == 0) return -1; /* no expiry */

    int64_t remaining = (e->expire_at - now_ms()) / 1000;
    return remaining > 0 ? remaining : 0;
}

int store_persist(store_t *s, const char *key) {
    store_entry_t *e = check_expiry(s, key);
    if (!e) return 0;
    e->expire_at = 0;
    return 1;
}

/* ensure the key holds a list, creating one if it doesn't exist */
static store_entry_t *ensure_list(store_t *s, const char *key) {
    store_entry_t *e = check_expiry(s, key);
    if (e) {
        if (e->type != CK_LIST) return NULL;
        return e;
    }

    e = ck_malloc(sizeof(store_entry_t));
    e->type = CK_LIST;
    e->list = list_create(free);
    e->expire_at = 0;
    e->last_access = now_ms();
    e->mem_usage = sizeof(store_entry_t) + sizeof(list_t) + strlen(key) + 1;

    ck_mem_track_alloc(e->mem_usage);
    ht_set(s->data, key, e);
    return e;
}

int store_lpush(store_t *s, const char *key, const char *value) {
    store_entry_t *e = ensure_list(s, key);
    if (!e) return -1;
    char *v = ck_strdup(value);
    size_t added = strlen(value) + 1 + sizeof(list_node_t);
    e->mem_usage += added;
    ck_mem_track_alloc(added);
    list_lpush(e->list, v);
    return (int)list_length(e->list);
}

int store_rpush(store_t *s, const char *key, const char *value) {
    store_entry_t *e = ensure_list(s, key);
    if (!e) return -1;
    char *v = ck_strdup(value);
    size_t added = strlen(value) + 1 + sizeof(list_node_t);
    e->mem_usage += added;
    ck_mem_track_alloc(added);
    list_rpush(e->list, v);
    return (int)list_length(e->list);
}

char *store_lpop(store_t *s, const char *key) {
    store_entry_t *e = check_expiry(s, key);
    if (!e || e->type != CK_LIST) return NULL;
    char *v = (char *)list_lpop(e->list);
    if (v) {
        size_t freed = strlen(v) + 1 + sizeof(list_node_t);
        e->mem_usage -= freed;
        ck_mem_track_free(freed);
    }
    /* auto-delete empty list keys */
    if (list_length(e->list) == 0) {
        ht_delete(s->data, key);
    }
    return v;
}

char *store_rpop(store_t *s, const char *key) {
    store_entry_t *e = check_expiry(s, key);
    if (!e || e->type != CK_LIST) return NULL;
    char *v = (char *)list_rpop(e->list);
    if (v) {
        size_t freed = strlen(v) + 1 + sizeof(list_node_t);
        e->mem_usage -= freed;
        ck_mem_track_free(freed);
    }
    if (list_length(e->list) == 0) {
        ht_delete(s->data, key);
    }
    return v;
}

int store_lrange(store_t *s, const char *key, int start, int stop,
                 char **out, int max_out) {
    store_entry_t *e = check_expiry(s, key);
    if (!e || e->type != CK_LIST) return 0;
    return list_range(e->list, start, stop, (void **)out, max_out);
}

int store_llen(store_t *s, const char *key) {
    store_entry_t *e = check_expiry(s, key);
    if (!e || e->type != CK_LIST) return 0;
    return (int)list_length(e->list);
}

/* ensure the key holds a hash, creating one if it doesn't exist */
static store_entry_t *ensure_hash(store_t *s, const char *key) {
    store_entry_t *e = check_expiry(s, key);
    if (e) {
        if (e->type != CK_HASH) return NULL;
        return e;
    }

    e = ck_malloc(sizeof(store_entry_t));
    e->type = CK_HASH;
    e->hash = ht_create(16, free);
    e->expire_at = 0;
    e->last_access = now_ms();
    e->mem_usage = sizeof(store_entry_t) + sizeof(hashtable_t) + strlen(key) + 1;

    ck_mem_track_alloc(e->mem_usage);
    ht_set(s->data, key, e);
    return e;
}

int store_hset(store_t *s, const char *key, const char *field, const char *value) {
    store_entry_t *e = ensure_hash(s, key);
    if (!e) return -1;

    int is_new = !ht_exists(e->hash, field);
    char *v = ck_strdup(value);
    ht_set(e->hash, field, v);

    if (is_new) {
        size_t added = strlen(field) + 1 + strlen(value) + 1;
        e->mem_usage += added;
        ck_mem_track_alloc(added);
    }
    return is_new ? 1 : 0;
}

char *store_hget(store_t *s, const char *key, const char *field) {
    store_entry_t *e = check_expiry(s, key);
    if (!e || e->type != CK_HASH) return NULL;
    return (char *)ht_get(e->hash, field);
}

int store_hdel(store_t *s, const char *key, const char *field) {
    store_entry_t *e = check_expiry(s, key);
    if (!e || e->type != CK_HASH) return 0;
    int deleted = ht_delete(e->hash, field);

    if (deleted && ht_count(e->hash) == 0) {
        ht_delete(s->data, key);
    }
    return deleted;
}

int store_hgetall(store_t *s, const char *key, char ***fields, char ***values, int *count) {
    store_entry_t *e = check_expiry(s, key);
    if (!e || e->type != CK_HASH) {
        *count = 0;
        return 0;
    }

    int n = (int)ht_count(e->hash);
    *count = n;
    if (n == 0) return 0;

    *fields = ck_malloc(sizeof(char *) * (size_t)n);
    *values = ck_malloc(sizeof(char *) * (size_t)n);

    ht_iter_t iter;
    ht_iter_init(&iter, e->hash);
    const char *f;
    void *v;
    int i = 0;
    while (ht_iter_next(&iter, &f, &v) && i < n) {
        (*fields)[i] = (char *)f;
        (*values)[i] = (char *)v;
        i++;
    }
    return 0;
}

int store_incr(store_t *s, const char *key, int64_t *result) {
    store_entry_t *e = check_expiry(s, key);
    if (!e) {
        store_set_int(s, key, 1);
        *result = 1;
        return 0;
    }

    int64_t val;
    if (e->type == CK_INT) {
        val = e->integer;
    } else if (e->type == CK_STRING) {
        if (ck_str_to_int64(e->str, &val) != 0) return -1;
    } else {
        return -1;
    }

    val++;
    store_set_int(s, key, val);
    *result = val;
    return 0;
}

int store_decr(store_t *s, const char *key, int64_t *result) {
    store_entry_t *e = check_expiry(s, key);
    if (!e) {
        store_set_int(s, key, -1);
        *result = -1;
        return 0;
    }

    int64_t val;
    if (e->type == CK_INT) {
        val = e->integer;
    } else if (e->type == CK_STRING) {
        if (ck_str_to_int64(e->str, &val) != 0) return -1;
    } else {
        return -1;
    }

    val--;
    store_set_int(s, key, val);
    *result = val;
    return 0;
}

size_t store_dbsize(store_t *s) {
    return ht_count(s->data);
}

void store_flushdb(store_t *s) {
    ht_destroy(s->data);
    s->data = ht_create(64, free_entry);
}

int store_keys(store_t *s, const char *pattern, char ***out, int *count) {
    size_t cap = 64;
    size_t n = 0;
    char **keys = ck_malloc(sizeof(char *) * cap);

    ht_iter_t iter;
    ht_iter_init(&iter, s->data);
    const char *key;
    void *val;

    while (ht_iter_next(&iter, &key, &val)) {
        store_entry_t *e = (store_entry_t *)val;
        if (store_is_expired(e)) continue;

        if (ck_glob_match(pattern, key)) {
            if (n >= cap) {
                cap *= 2;
                keys = ck_realloc(keys, sizeof(char *) * cap);
            }
            keys[n++] = ck_strdup(key);
        }
    }

    *out = keys;
    *count = (int)n;
    return 0;
}

int store_expire_cycle(store_t *s, int sample_size) {
    int expired = 0;
    for (int i = 0; i < sample_size; i++) {
        const char *key;
        if (!ht_random_key(s->data, &key)) break;

        store_entry_t *e = (store_entry_t *)ht_get(s->data, key);
        if (e && store_is_expired(e)) {
            /* need to copy key since ht_delete will free it */
            char *key_copy = ck_strdup(key);
            ht_delete(s->data, key_copy);
            free(key_copy);
            expired++;
        }
    }
    return expired;
}

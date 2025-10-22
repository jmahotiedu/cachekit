#ifndef CK_STORE_H
#define CK_STORE_H

#include "hashtable.h"
#include "list.h"
#include <stdint.h>
#include <stddef.h>

typedef enum {
    CK_STRING,
    CK_INT,
    CK_LIST,
    CK_HASH
} ck_type_t;

typedef struct {
    ck_type_t type;
    union {
        char *str;
        int64_t integer;
        list_t *list;
        hashtable_t *hash;
    };
    int64_t expire_at;  /* absolute ms timestamp, 0 = no expiry */
    int64_t last_access; /* for LRU */
    size_t mem_usage;    /* approximate memory footprint */
} store_entry_t;

typedef struct {
    hashtable_t *data;
    size_t maxmemory;     /* 0 = unlimited */
} store_t;

store_t *store_create(void);
void store_destroy(store_t *store);

/* basic ops */
int store_set(store_t *s, const char *key, const char *value);
int store_set_int(store_t *s, const char *key, int64_t value);
const char *store_get(store_t *s, const char *key);
int store_get_int(store_t *s, const char *key, int64_t *out);
store_entry_t *store_get_entry(store_t *s, const char *key);
int store_del(store_t *s, const char *key);
int store_exists(store_t *s, const char *key);

/* type check */
ck_type_t store_type(store_t *s, const char *key);

/* TTL */
int store_expire(store_t *s, const char *key, int64_t seconds);
int64_t store_ttl(store_t *s, const char *key);
int store_persist(store_t *s, const char *key);

/* list ops */
int store_lpush(store_t *s, const char *key, const char *value);
int store_rpush(store_t *s, const char *key, const char *value);
char *store_lpop(store_t *s, const char *key);
char *store_rpop(store_t *s, const char *key);
int store_lrange(store_t *s, const char *key, int start, int stop,
                 char **out, int max_out);
int store_llen(store_t *s, const char *key);

/* hash ops */
int store_hset(store_t *s, const char *key, const char *field, const char *value);
char *store_hget(store_t *s, const char *key, const char *field);
int store_hdel(store_t *s, const char *key, const char *field);
int store_hgetall(store_t *s, const char *key, char ***fields, char ***values, int *count);

/* increment/decrement */
int store_incr(store_t *s, const char *key, int64_t *result);
int store_decr(store_t *s, const char *key, int64_t *result);

/* db ops */
size_t store_dbsize(store_t *s);
void store_flushdb(store_t *s);

/* collect matching keys (caller frees returned array and strings) */
int store_keys(store_t *s, const char *pattern, char ***out, int *count);

/* passive expiration check */
int store_is_expired(store_entry_t *e);

/* active expiration: check a random sample and delete expired keys */
int store_expire_cycle(store_t *s, int sample_size);

#endif

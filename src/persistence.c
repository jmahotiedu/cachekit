#include "persistence.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* binary write helpers */
static int write_u8(FILE *f, uint8_t v) {
    return fwrite(&v, 1, 1, f) == 1 ? 0 : -1;
}

static int write_u32(FILE *f, uint32_t v) {
    return fwrite(&v, 4, 1, f) == 1 ? 0 : -1;
}

static int write_u64(FILE *f, uint64_t v) {
    return fwrite(&v, 8, 1, f) == 1 ? 0 : -1;
}

static int write_i64(FILE *f, int64_t v) {
    return fwrite(&v, 8, 1, f) == 1 ? 0 : -1;
}

static int write_str(FILE *f, const char *s) {
    uint32_t len = (uint32_t)strlen(s);
    if (write_u32(f, len) != 0) return -1;
    return fwrite(s, 1, len, f) == len ? 0 : -1;
}

/* binary read helpers */
static int read_u8(FILE *f, uint8_t *v) {
    return fread(v, 1, 1, f) == 1 ? 0 : -1;
}

static int read_u32(FILE *f, uint32_t *v) {
    return fread(v, 4, 1, f) == 1 ? 0 : -1;
}

static int read_u64(FILE *f, uint64_t *v) {
    return fread(v, 8, 1, f) == 1 ? 0 : -1;
}

static int read_i64(FILE *f, int64_t *v) {
    return fread(v, 8, 1, f) == 1 ? 0 : -1;
}

static char *read_str(FILE *f) {
    uint32_t len;
    if (read_u32(f, &len) != 0) return NULL;
    if (len > 64 * 1024 * 1024) return NULL; /* sanity limit */

    char *s = ck_malloc(len + 1);
    if (fread(s, 1, len, f) != len) {
        free(s);
        return NULL;
    }
    s[len] = '\0';
    return s;
}

int persistence_save(store_t *s, const char *filename) {
    char tmpname[256];
    snprintf(tmpname, sizeof(tmpname), "%s.tmp", filename);

    FILE *f = fopen(tmpname, "wb");
    if (!f) {
        ck_log(CK_LOG_ERROR, "failed to open %s for writing", tmpname);
        return -1;
    }

    /* header */
    fwrite(CK_RDB_MAGIC, 1, 8, f);
    write_u32(f, CK_RDB_VERSION);
    write_u64(f, (uint64_t)time(NULL));

    ht_iter_t iter;
    ht_iter_init(&iter, s->data);
    const char *key;
    void *val;

    while (ht_iter_next(&iter, &key, &val)) {
        store_entry_t *e = (store_entry_t *)val;

        /* skip expired keys */
        if (store_is_expired(e)) continue;

        switch (e->type) {
            case CK_STRING:
                write_u8(f, CK_RDB_TYPE_STRING);
                write_str(f, key);
                write_str(f, e->str);
                break;

            case CK_INT:
                write_u8(f, CK_RDB_TYPE_INT);
                write_str(f, key);
                write_i64(f, e->integer);
                break;

            case CK_LIST: {
                write_u8(f, CK_RDB_TYPE_LIST);
                write_str(f, key);
                uint32_t len = (uint32_t)list_length(e->list);
                write_u32(f, len);
                list_node_t *node = e->list->head;
                while (node) {
                    write_str(f, (const char *)node->value);
                    node = node->next;
                }
                break;
            }

            case CK_HASH: {
                write_u8(f, CK_RDB_TYPE_HASH);
                write_str(f, key);
                uint32_t cnt = (uint32_t)ht_count(e->hash);
                write_u32(f, cnt);

                ht_iter_t hiter;
                ht_iter_init(&hiter, e->hash);
                const char *field;
                void *hval;
                while (ht_iter_next(&hiter, &field, &hval)) {
                    write_str(f, field);
                    write_str(f, (const char *)hval);
                }
                break;
            }
        }

        /* write TTL */
        write_i64(f, e->expire_at);
    }

    write_u8(f, CK_RDB_EOF);
    fclose(f);

    /* atomic rename */
    remove(filename);
    if (rename(tmpname, filename) != 0) {
        ck_log(CK_LOG_ERROR, "failed to rename %s to %s", tmpname, filename);
        return -1;
    }

    ck_log(CK_LOG_INFO, "saved snapshot to %s", filename);
    return 0;
}

int persistence_load(store_t *s, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;

    /* verify magic */
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, CK_RDB_MAGIC, 8) != 0) {
        ck_log(CK_LOG_ERROR, "invalid snapshot magic");
        fclose(f);
        return -1;
    }

    uint32_t version;
    uint64_t timestamp;
    read_u32(f, &version);
    read_u64(f, &timestamp);

    if (version != CK_RDB_VERSION) {
        ck_log(CK_LOG_ERROR, "unsupported snapshot version %u", version);
        fclose(f);
        return -1;
    }

    int loaded = 0;
    while (1) {
        uint8_t type;
        if (read_u8(f, &type) != 0) break;
        if (type == CK_RDB_EOF) break;

        char *key = read_str(f);
        if (!key) break;

        int64_t expire_at;

        switch (type) {
            case CK_RDB_TYPE_STRING: {
                char *val = read_str(f);
                if (!val) { free(key); goto done; }
                read_i64(f, &expire_at);
                store_set(s, key, val);
                if (expire_at > 0) {
                    store_entry_t *e = (store_entry_t *)ht_get(s->data, key);
                    if (e) e->expire_at = expire_at;
                }
                free(val);
                break;
            }

            case CK_RDB_TYPE_INT: {
                int64_t val;
                read_i64(f, &val);
                read_i64(f, &expire_at);
                store_set_int(s, key, val);
                if (expire_at > 0) {
                    store_entry_t *e = (store_entry_t *)ht_get(s->data, key);
                    if (e) e->expire_at = expire_at;
                }
                break;
            }

            case CK_RDB_TYPE_LIST: {
                uint32_t len;
                read_u32(f, &len);
                for (uint32_t i = 0; i < len; i++) {
                    char *val = read_str(f);
                    if (!val) { free(key); goto done; }
                    store_rpush(s, key, val);
                    free(val);
                }
                read_i64(f, &expire_at);
                if (expire_at > 0) {
                    store_entry_t *e = (store_entry_t *)ht_get(s->data, key);
                    if (e) e->expire_at = expire_at;
                }
                break;
            }

            case CK_RDB_TYPE_HASH: {
                uint32_t cnt;
                read_u32(f, &cnt);
                for (uint32_t i = 0; i < cnt; i++) {
                    char *field = read_str(f);
                    char *val = read_str(f);
                    if (!field || !val) {
                        free(field);
                        free(val);
                        free(key);
                        goto done;
                    }
                    store_hset(s, key, field, val);
                    free(field);
                    free(val);
                }
                read_i64(f, &expire_at);
                if (expire_at > 0) {
                    store_entry_t *e = (store_entry_t *)ht_get(s->data, key);
                    if (e) e->expire_at = expire_at;
                }
                break;
            }

            default:
                ck_log(CK_LOG_ERROR, "unknown type marker 0x%02x", type);
                free(key);
                goto done;
        }

        free(key);
        loaded++;
    }

done:
    fclose(f);
    ck_log(CK_LOG_INFO, "loaded %d keys from %s", loaded, filename);
    return 0;
}

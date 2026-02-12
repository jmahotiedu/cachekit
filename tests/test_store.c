#include "store.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int n_fail;

static void ok(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        n_fail++;
    }
}

void test_store_basic(void) {
    store_t *s = store_create();
    ok(s != NULL, "store_create");

    ok(store_set(s, "k1", "v1") == 0, "set k1");
    const char *v = store_get(s, "v1");
    ok(v == NULL, "get wrong key returns NULL");
    v = store_get(s, "k1");
    ok(v != NULL && strcmp(v, "v1") == 0, "get k1");
    ok(store_dbsize(s) == 1, "dbsize 1");

    ok(store_set(s, "k2", "val2") == 0, "set k2");
    ok(store_del(s, "k1") == 1, "del k1");
    ok(store_get(s, "k1") == NULL, "get k1 after del");
    ok(store_get(s, "k2") != NULL, "get k2");
    ok(store_dbsize(s) == 1, "dbsize after del");

    store_flushdb(s);
    ok(store_dbsize(s) == 0, "dbsize after flushdb");

    store_destroy(s);
}

void test_store_int(void) {
    store_t *s = store_create();
    int64_t out;
    ok(store_set_int(s, "n", 42) == 0, "set_int");
    ok(store_get_int(s, "n", &out) == 0 && out == 42, "get_int");
    ok(store_incr(s, "n", &out) == 0 && out == 43, "incr");
    ok(store_decr(s, "n", &out) == 0 && out == 42, "decr");
    store_destroy(s);
}

void test_store_list(void) {
    store_t *s = store_create();
    ok(store_lpush(s, "l", "a") == 1, "lpush a");
    ok(store_lpush(s, "l", "b") == 2, "lpush b");
    char *out[4];
    int n = store_lrange(s, "l", 0, -1, out, 4);
    ok(n == 2 && out[0] != NULL && out[1] != NULL, "lrange");
    if (n >= 2) {
        ok(strcmp(out[0], "b") == 0 && strcmp(out[1], "a") == 0, "lrange order");
    }
    char *p = store_lpop(s, "l");
    ok(p != NULL && strcmp(p, "b") == 0, "lpop");
    free(p);
    store_destroy(s);
}

int test_store_run(void) {
    n_fail = 0;
    test_store_basic();
    test_store_int();
    test_store_list();
    return n_fail;
}

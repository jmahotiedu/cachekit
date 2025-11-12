#include "store.h"
#include "persistence.h"
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

int test_persistence_run(void) {
    n_fail = 0;
    const char *path = "build/test_save.ckdb";

    store_t *s = store_create();
    ok(s != NULL, "store_create");
    store_set(s, "k1", "v1");
    store_set(s, "k2", "val2");
    store_set_int(s, "n", 99);
    ok(store_dbsize(s) == 3, "dbsize 3");

    ok(persistence_save(s, path) == 0, "save");
    store_destroy(s);

    s = store_create();
    ok(persistence_load(s, path) == 0, "load");
    ok(store_dbsize(s) == 3, "dbsize after load");
    const char *v = store_get(s, "k1");
    ok(v != NULL && strcmp(v, "v1") == 0, "get k1");
    v = store_get(s, "k2");
    ok(v != NULL && strcmp(v, "val2") == 0, "get k2");
    int64_t n;
    ok(store_get_int(s, "n", &n) == 1 && n == 99, "get n");
    store_destroy(s);

    remove(path);
    return n_fail;
}

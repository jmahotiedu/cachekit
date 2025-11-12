#include "hashtable.h"
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

static void free_str(void *p) { free(p); }

int test_hashtable_run(void) {
    n_fail = 0;
    hashtable_t *ht = ht_create(16, free_str);
    ok(ht != NULL, "ht_create");
    ok(ht_count(ht) == 0, "count 0");

    char *v1 = strdup("a");
    ok(ht_set(ht, "k1", v1) == 1, "set k1");
    ok(ht_get(ht, "k1") == v1, "get k1");
    ok(ht_exists(ht, "k1") == 1, "exists k1");
    ok(ht_get(ht, "k2") == NULL, "get missing");

    ok(ht_set(ht, "k2", strdup("b")) == 1, "set k2");
    ok(ht_delete(ht, "k1") == 1, "del k1");
    ok(ht_get(ht, "k1") == NULL, "get after del");
    ok(ht_count(ht) == 1, "count after del");

    for (int i = 0; i < 100; i++) {
        char key[16];
        snprintf(key, sizeof(key), "key%d", i);
        ht_set(ht, key, strdup(key));
    }
    ok(ht_count(ht) == 101, "count after 100 inserts");

    ht_destroy(ht);
    return n_fail;
}

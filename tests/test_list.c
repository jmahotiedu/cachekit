#include "list.h"
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

int test_list_run(void) {
    n_fail = 0;
    list_t *L = list_create(free_str);
    ok(L != NULL, "list_create");
    ok(list_length(L) == 0, "length 0");

    list_rpush(L, strdup("a"));
    list_rpush(L, strdup("b"));
    list_rpush(L, strdup("c"));
    ok(list_length(L) == 3, "length 3");
    ok(strcmp(list_index(L, 0), "a") == 0, "index 0");
    ok(strcmp(list_index(L, -1), "c") == 0, "index -1");

    void *out[4];
    int n = list_range(L, 0, -1, out, 4);
    ok(n == 3 && strcmp(out[0], "a") == 0 && strcmp(out[2], "c") == 0, "range");

    char *p = list_lpop(L);
    ok(p != NULL && strcmp(p, "a") == 0, "lpop");
    free(p);
    ok(list_length(L) == 2, "length after lpop");

    list_lpush(L, strdup("z"));
    ok(strcmp(list_index(L, 0), "z") == 0, "lpush");

    list_destroy(L);
    return n_fail;
}

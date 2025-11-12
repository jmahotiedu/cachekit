#include <stdio.h>
#include <stdlib.h>

extern int test_store_run(void);
extern int test_protocol_run(void);
extern int test_hashtable_run(void);
extern int test_list_run(void);
extern int test_persistence_run(void);

int main(void) {
    int fail = 0;
    fail += test_hashtable_run();
    fail += test_list_run();
    fail += test_store_run();
    fail += test_protocol_run();
    fail += test_persistence_run();
    if (fail > 0) {
        fprintf(stderr, "%d test(s) failed\n", fail);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}

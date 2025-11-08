#include "server.h"
#include "store.h"
#include "persistence.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_PORT 6380
#define DEFAULT_RDB "dump.ckdb"

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s [-p port] [-d rdb_file]\n", prog);
    fprintf(stderr, "  -p port     listen port (default %d)\n", DEFAULT_PORT);
    fprintf(stderr, "  -d file     RDB snapshot path (default %s)\n", DEFAULT_RDB);
}

int main(int argc, char **argv) {
    server_config_t config = {
        .port = DEFAULT_PORT,
        .rdb_filename = DEFAULT_RDB,
        .max_clients = 64
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            int p = atoi(argv[i + 1]);
            if (p <= 0 || p > 65535) {
                fprintf(stderr, "invalid port\n");
                return 1;
            }
            config.port = (uint16_t)p;
            i++;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            config.rdb_filename = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    ck_log_set_level(CK_LOG_INFO);

    store_t *store = store_create();
    if (!store) {
        ck_log(CK_LOG_ERROR, "store_create failed");
        return 1;
    }

    if (persistence_load(store, config.rdb_filename) == 0) {
        ck_log(CK_LOG_INFO, "loaded RDB from %s", config.rdb_filename);
    }

    command_ctx_t ctx = {
        .store = store,
        .rdb_filename = config.rdb_filename,
        .start_time = ck_time_ms(),
        .commands_processed = 0,
        .connected_clients = 0
    };

    server_run(&config, &ctx);

    store_destroy(store);
    return 0;
}

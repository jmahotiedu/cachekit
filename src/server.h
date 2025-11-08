#ifndef CK_SERVER_H
#define CK_SERVER_H

#include "command.h"
#include <stdint.h>

typedef struct server_config {
    uint16_t port;
    const char *rdb_filename;
    int max_clients;
} server_config_t;

/* run event loop; returns on error or shutdown */
void server_run(server_config_t *config, command_ctx_t *ctx);

#endif

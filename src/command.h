#ifndef CK_COMMAND_H
#define CK_COMMAND_H

#include "protocol.h"
#include "store.h"

typedef struct {
    store_t *store;
    const char *rdb_filename;
    int64_t start_time;
    int64_t commands_processed;
    int connected_clients;
} command_ctx_t;

/* dispatch a parsed RESP command and write the response */
void command_dispatch(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out);

#endif

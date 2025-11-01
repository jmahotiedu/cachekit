#include "command.h"
#include "eviction.h"
#include "persistence.h"
#include "util.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define ERR_WRONGTYPE "WRONGTYPE Operation against a key holding the wrong kind of value"
#define ERR_SYNTAX    "ERR syntax error"
#define ERR_ARGS      "ERR wrong number of arguments for '%s' command"

/* case-insensitive compare */
static int cmd_eq(const char *a, const char *b) {
    return strcasecmp(a, b) == 0;
}

static char *get_arg(resp_value_t *cmd, int idx) {
    if (cmd->type != RESP_ARRAY || idx >= cmd->array.count) return NULL;
    resp_value_t *v = cmd->array.elements[idx];
    if (v->type == RESP_BULK_STRING || v->type == RESP_SIMPLE_STRING) {
        return v->str;
    }
    return NULL;
}

static int arg_count(resp_value_t *cmd) {
    if (cmd->type != RESP_ARRAY) return 0;
    return cmd->array.count;
}

static void cmd_ping(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    (void)ctx;
    if (arg_count(cmd) > 1) {
        char *msg = get_arg(cmd, 1);
        if (msg) {
            resp_write_bulk_string(out, msg, strlen(msg));
            return;
        }
    }
    resp_write_simple_string(out, "PONG");
}

static void cmd_echo(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    (void)ctx;
    if (arg_count(cmd) < 2) {
        resp_write_error(out, "ERR wrong number of arguments for 'echo' command");
        return;
    }
    char *msg = get_arg(cmd, 1);
    resp_write_bulk_string(out, msg, strlen(msg));
}

static void cmd_set(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    int argc = arg_count(cmd);
    if (argc < 3) {
        resp_write_error(out, "ERR wrong number of arguments for 'set' command");
        return;
    }

    char *key = get_arg(cmd, 1);
    char *value = get_arg(cmd, 2);
    store_set(ctx->store, key, value);

    /* handle EX option */
    if (argc >= 5) {
        char *opt = get_arg(cmd, 3);
        if (opt && cmd_eq(opt, "EX")) {
            char *secs_str = get_arg(cmd, 4);
            int64_t secs;
            if (ck_str_to_int64(secs_str, &secs) == 0 && secs > 0) {
                store_expire(ctx->store, key, secs);
            }
        }
    }

    eviction_check(ctx->store);
    resp_write_simple_string(out, "OK");
}

static void cmd_get(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 2) {
        resp_write_error(out, "ERR wrong number of arguments for 'get' command");
        return;
    }

    char *key = get_arg(cmd, 1);
    store_entry_t *e = store_get_entry(ctx->store, key);

    if (!e) {
        resp_write_null(out);
        return;
    }

    if (e->type == CK_STRING) {
        resp_write_bulk_string(out, e->str, strlen(e->str));
    } else if (e->type == CK_INT) {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "%lld", (long long)e->integer);
        resp_write_bulk_string(out, buf, (size_t)n);
    } else {
        resp_write_error(out, ERR_WRONGTYPE);
    }
}

static void cmd_del(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    int argc = arg_count(cmd);
    if (argc < 2) {
        resp_write_error(out, "ERR wrong number of arguments for 'del' command");
        return;
    }

    int deleted = 0;
    for (int i = 1; i < argc; i++) {
        char *key = get_arg(cmd, i);
        if (key) deleted += store_del(ctx->store, key);
    }
    resp_write_integer(out, deleted);
}

static void cmd_incr(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 2) {
        resp_write_error(out, "ERR wrong number of arguments for 'incr' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    int64_t result;
    if (store_incr(ctx->store, key, &result) != 0) {
        resp_write_error(out, "ERR value is not an integer or out of range");
        return;
    }
    resp_write_integer(out, result);
}

static void cmd_decr(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 2) {
        resp_write_error(out, "ERR wrong number of arguments for 'decr' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    int64_t result;
    if (store_decr(ctx->store, key, &result) != 0) {
        resp_write_error(out, "ERR value is not an integer or out of range");
        return;
    }
    resp_write_integer(out, result);
}

static void cmd_lpush(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 3) {
        resp_write_error(out, "ERR wrong number of arguments for 'lpush' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    char *value = get_arg(cmd, 2);
    int len = store_lpush(ctx->store, key, value);
    if (len < 0) {
        resp_write_error(out, ERR_WRONGTYPE);
        return;
    }
    eviction_check(ctx->store);
    resp_write_integer(out, len);
}

static void cmd_rpush(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 3) {
        resp_write_error(out, "ERR wrong number of arguments for 'rpush' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    char *value = get_arg(cmd, 2);
    int len = store_rpush(ctx->store, key, value);
    if (len < 0) {
        resp_write_error(out, ERR_WRONGTYPE);
        return;
    }
    eviction_check(ctx->store);
    resp_write_integer(out, len);
}

static void cmd_lpop(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 2) {
        resp_write_error(out, "ERR wrong number of arguments for 'lpop' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    char *val = store_lpop(ctx->store, key);
    if (!val) {
        resp_write_null(out);
    } else {
        resp_write_bulk_string(out, val, strlen(val));
        free(val);
    }
}

static void cmd_rpop(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 2) {
        resp_write_error(out, "ERR wrong number of arguments for 'rpop' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    char *val = store_rpop(ctx->store, key);
    if (!val) {
        resp_write_null(out);
    } else {
        resp_write_bulk_string(out, val, strlen(val));
        free(val);
    }
}

static void cmd_lrange(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 4) {
        resp_write_error(out, "ERR wrong number of arguments for 'lrange' command");
        return;
    }

    char *key = get_arg(cmd, 1);
    int64_t start, stop;
    if (ck_str_to_int64(get_arg(cmd, 2), &start) != 0 ||
        ck_str_to_int64(get_arg(cmd, 3), &stop) != 0) {
        resp_write_error(out, "ERR value is not an integer or out of range");
        return;
    }

    /* cap range retrieval buffer */
    int max_out = 4096;
    char **items = ck_malloc(sizeof(char *) * (size_t)max_out);
    int count = store_lrange(ctx->store, key, (int)start, (int)stop, items, max_out);

    resp_write_array_header(out, count);
    for (int i = 0; i < count; i++) {
        resp_write_bulk_string(out, items[i], strlen(items[i]));
    }
    free(items);
}

static void cmd_llen(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 2) {
        resp_write_error(out, "ERR wrong number of arguments for 'llen' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    resp_write_integer(out, store_llen(ctx->store, key));
}

static void cmd_hset(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 4) {
        resp_write_error(out, "ERR wrong number of arguments for 'hset' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    char *field = get_arg(cmd, 2);
    char *value = get_arg(cmd, 3);

    int result = store_hset(ctx->store, key, field, value);
    if (result < 0) {
        resp_write_error(out, ERR_WRONGTYPE);
        return;
    }
    eviction_check(ctx->store);
    resp_write_integer(out, result);
}

static void cmd_hget(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 3) {
        resp_write_error(out, "ERR wrong number of arguments for 'hget' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    char *field = get_arg(cmd, 2);
    char *val = store_hget(ctx->store, key, field);
    if (!val) {
        resp_write_null(out);
    } else {
        resp_write_bulk_string(out, val, strlen(val));
    }
}

static void cmd_hdel(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 3) {
        resp_write_error(out, "ERR wrong number of arguments for 'hdel' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    char *field = get_arg(cmd, 2);
    resp_write_integer(out, store_hdel(ctx->store, key, field));
}

static void cmd_hgetall(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 2) {
        resp_write_error(out, "ERR wrong number of arguments for 'hgetall' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    char **fields, **values;
    int count;
    store_hgetall(ctx->store, key, &fields, &values, &count);

    resp_write_array_header(out, count * 2);
    for (int i = 0; i < count; i++) {
        resp_write_bulk_string(out, fields[i], strlen(fields[i]));
        resp_write_bulk_string(out, values[i], strlen(values[i]));
    }
    if (count > 0) {
        free(fields);
        free(values);
    }
}

static void cmd_expire(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 3) {
        resp_write_error(out, "ERR wrong number of arguments for 'expire' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    int64_t secs;
    if (ck_str_to_int64(get_arg(cmd, 2), &secs) != 0) {
        resp_write_error(out, "ERR value is not an integer or out of range");
        return;
    }
    resp_write_integer(out, store_expire(ctx->store, key, secs));
}

static void cmd_ttl(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 2) {
        resp_write_error(out, "ERR wrong number of arguments for 'ttl' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    resp_write_integer(out, store_ttl(ctx->store, key));
}

static void cmd_persist(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 2) {
        resp_write_error(out, "ERR wrong number of arguments for 'persist' command");
        return;
    }
    char *key = get_arg(cmd, 1);
    resp_write_integer(out, store_persist(ctx->store, key));
}

static void cmd_keys(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (arg_count(cmd) < 2) {
        resp_write_error(out, "ERR wrong number of arguments for 'keys' command");
        return;
    }
    char *pattern = get_arg(cmd, 1);
    char **keys;
    int count;
    store_keys(ctx->store, pattern, &keys, &count);

    resp_write_array_header(out, count);
    for (int i = 0; i < count; i++) {
        resp_write_bulk_string(out, keys[i], strlen(keys[i]));
        free(keys[i]);
    }
    free(keys);
}

static void cmd_dbsize(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    (void)cmd;
    resp_write_integer(out, (int64_t)store_dbsize(ctx->store));
}

static void cmd_flushdb(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    (void)cmd;
    store_flushdb(ctx->store);
    resp_write_simple_string(out, "OK");
}

static void cmd_save(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    (void)cmd;
    int rc = persistence_save(ctx->store, ctx->rdb_filename);
    if (rc == 0) {
        resp_write_simple_string(out, "OK");
    } else {
        resp_write_error(out, "ERR snapshot save failed");
    }
}

static void cmd_info(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    (void)cmd;
    char buf[1024];
    int64_t uptime = (ck_time_ms() - ctx->start_time) / 1000;

    int n = snprintf(buf, sizeof(buf),
        "# Server\r\n"
        "cachekit_version:0.1.0\r\n"
        "uptime_in_seconds:%lld\r\n"
        "connected_clients:%d\r\n"
        "used_memory:%zu\r\n"
        "total_commands_processed:%lld\r\n"
        "db0:keys=%zu\r\n",
        (long long)uptime,
        ctx->connected_clients,
        ck_mem_used(),
        (long long)ctx->commands_processed,
        store_dbsize(ctx->store)
    );

    resp_write_bulk_string(out, buf, (size_t)n);
}

void command_dispatch(command_ctx_t *ctx, resp_value_t *cmd, resp_buf_t *out) {
    if (!cmd || (cmd->type != RESP_ARRAY) || cmd->array.count < 1) {
        resp_write_error(out, "ERR invalid command format");
        return;
    }

    char *name = get_arg(cmd, 0);
    if (!name) {
        resp_write_error(out, "ERR invalid command");
        return;
    }

    ctx->commands_processed++;

    /* run passive expiration on a few random keys each command */
    store_expire_cycle(ctx->store, 3);

    if (cmd_eq(name, "PING"))         cmd_ping(ctx, cmd, out);
    else if (cmd_eq(name, "ECHO"))    cmd_echo(ctx, cmd, out);
    else if (cmd_eq(name, "SET"))     cmd_set(ctx, cmd, out);
    else if (cmd_eq(name, "GET"))     cmd_get(ctx, cmd, out);
    else if (cmd_eq(name, "DEL"))     cmd_del(ctx, cmd, out);
    else if (cmd_eq(name, "INCR"))    cmd_incr(ctx, cmd, out);
    else if (cmd_eq(name, "DECR"))    cmd_decr(ctx, cmd, out);
    else if (cmd_eq(name, "LPUSH"))   cmd_lpush(ctx, cmd, out);
    else if (cmd_eq(name, "RPUSH"))   cmd_rpush(ctx, cmd, out);
    else if (cmd_eq(name, "LPOP"))    cmd_lpop(ctx, cmd, out);
    else if (cmd_eq(name, "RPOP"))    cmd_rpop(ctx, cmd, out);
    else if (cmd_eq(name, "LRANGE"))  cmd_lrange(ctx, cmd, out);
    else if (cmd_eq(name, "LLEN"))    cmd_llen(ctx, cmd, out);
    else if (cmd_eq(name, "HSET"))    cmd_hset(ctx, cmd, out);
    else if (cmd_eq(name, "HGET"))    cmd_hget(ctx, cmd, out);
    else if (cmd_eq(name, "HDEL"))    cmd_hdel(ctx, cmd, out);
    else if (cmd_eq(name, "HGETALL")) cmd_hgetall(ctx, cmd, out);
    else if (cmd_eq(name, "EXPIRE"))  cmd_expire(ctx, cmd, out);
    else if (cmd_eq(name, "TTL"))     cmd_ttl(ctx, cmd, out);
    else if (cmd_eq(name, "PERSIST")) cmd_persist(ctx, cmd, out);
    else if (cmd_eq(name, "KEYS"))    cmd_keys(ctx, cmd, out);
    else if (cmd_eq(name, "DBSIZE"))  cmd_dbsize(ctx, cmd, out);
    else if (cmd_eq(name, "FLUSHDB")) cmd_flushdb(ctx, cmd, out);
    else if (cmd_eq(name, "SAVE"))    cmd_save(ctx, cmd, out);
    else if (cmd_eq(name, "INFO"))    cmd_info(ctx, cmd, out);
    else {
        char errbuf[128];
        snprintf(errbuf, sizeof(errbuf), "ERR unknown command '%s'", name);
        resp_write_error(out, errbuf);
    }
}

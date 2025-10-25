#include "protocol.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define RESP_BUF_INIT_CAP 256

void resp_parser_init(resp_parser_t *p) {
    p->cap = RESP_BUF_INIT_CAP;
    p->buf = ck_malloc(p->cap);
    p->len = 0;
    p->pos = 0;
}

void resp_parser_destroy(resp_parser_t *p) {
    free(p->buf);
    p->buf = NULL;
    p->len = 0;
    p->cap = 0;
    p->pos = 0;
}

void resp_parser_feed(resp_parser_t *p, const char *data, size_t len) {
    /* shift unconsumed data to front if needed */
    if (p->pos > 0) {
        size_t remaining = p->len - p->pos;
        if (remaining > 0) {
            memmove(p->buf, p->buf + p->pos, remaining);
        }
        p->len = remaining;
        p->pos = 0;
    }

    if (p->len + len > p->cap) {
        while (p->len + len > p->cap) p->cap *= 2;
        p->buf = ck_realloc(p->buf, p->cap);
    }
    memcpy(p->buf + p->len, data, len);
    p->len += len;
}

/* find \r\n starting from offset, return index of \r or -1 */
static int find_crlf(resp_parser_t *p, size_t from) {
    for (size_t i = from; i + 1 < p->len; i++) {
        if (p->buf[i] == '\r' && p->buf[i + 1] == '\n') {
            return (int)i;
        }
    }
    return -1;
}

/* forward declaration for recursive parsing */
static int parse_value(resp_parser_t *p, resp_value_t **out);

static int parse_line_value(resp_parser_t *p, resp_type_t type, resp_value_t **out) {
    int crlf = find_crlf(p, p->pos + 1);
    if (crlf < 0) return 0;

    size_t start = p->pos + 1;
    size_t slen = (size_t)crlf - start;

    resp_value_t *v = ck_malloc(sizeof(resp_value_t));
    v->type = type;

    if (type == RESP_INTEGER) {
        char tmp[32];
        if (slen >= sizeof(tmp)) slen = sizeof(tmp) - 1;
        memcpy(tmp, p->buf + start, slen);
        tmp[slen] = '\0';
        v->integer = strtoll(tmp, NULL, 10);
    } else {
        v->str = ck_strndup(p->buf + start, slen);
    }

    p->pos = (size_t)crlf + 2;
    *out = v;
    return 1;
}

static int parse_bulk_string(resp_parser_t *p, resp_value_t **out) {
    int crlf = find_crlf(p, p->pos + 1);
    if (crlf < 0) return 0;

    char tmp[32];
    size_t start = p->pos + 1;
    size_t numlen = (size_t)crlf - start;
    if (numlen >= sizeof(tmp)) return 0;
    memcpy(tmp, p->buf + start, numlen);
    tmp[numlen] = '\0';
    int blen = atoi(tmp);

    /* null bulk string */
    if (blen < 0) {
        resp_value_t *v = ck_malloc(sizeof(resp_value_t));
        v->type = RESP_NIL;
        v->str = NULL;
        p->pos = (size_t)crlf + 2;
        *out = v;
        return 1;
    }

    size_t data_start = (size_t)crlf + 2;
    /* need blen bytes + \r\n */
    if (data_start + (size_t)blen + 2 > p->len) return 0;

    resp_value_t *v = ck_malloc(sizeof(resp_value_t));
    v->type = RESP_BULK_STRING;
    v->str = ck_strndup(p->buf + data_start, (size_t)blen);

    p->pos = data_start + (size_t)blen + 2;
    *out = v;
    return 1;
}

static int parse_array(resp_parser_t *p, resp_value_t **out) {
    int crlf = find_crlf(p, p->pos + 1);
    if (crlf < 0) return 0;

    char tmp[32];
    size_t start = p->pos + 1;
    size_t numlen = (size_t)crlf - start;
    if (numlen >= sizeof(tmp)) return 0;
    memcpy(tmp, p->buf + start, numlen);
    tmp[numlen] = '\0';
    int count = atoi(tmp);

    if (count < 0) {
        resp_value_t *v = ck_malloc(sizeof(resp_value_t));
        v->type = RESP_NIL;
        v->str = NULL;
        p->pos = (size_t)crlf + 2;
        *out = v;
        return 1;
    }

    /* save position so we can rewind on incomplete data */
    size_t saved = p->pos;
    p->pos = (size_t)crlf + 2;

    resp_value_t **elements = NULL;
    if (count > 0) {
        elements = ck_malloc(sizeof(resp_value_t *) * (size_t)count);
    }

    for (int i = 0; i < count; i++) {
        if (!parse_value(p, &elements[i])) {
            /* incomplete - free what we parsed and rewind */
            for (int j = 0; j < i; j++) {
                resp_value_free(elements[j]);
            }
            free(elements);
            p->pos = saved;
            return 0;
        }
    }

    resp_value_t *v = ck_malloc(sizeof(resp_value_t));
    v->type = RESP_ARRAY;
    v->array.elements = elements;
    v->array.count = count;
    *out = v;
    return 1;
}

static int parse_value(resp_parser_t *p, resp_value_t **out) {
    if (p->pos >= p->len) return 0;

    char type = p->buf[p->pos];
    switch (type) {
        case '+': return parse_line_value(p, RESP_SIMPLE_STRING, out);
        case '-': return parse_line_value(p, RESP_ERROR, out);
        case ':': return parse_line_value(p, RESP_INTEGER, out);
        case '$': return parse_bulk_string(p, out);
        case '*': return parse_array(p, out);
        default:
            /* inline command: treat everything up to \r\n as a simple string */
            return parse_line_value(p, RESP_SIMPLE_STRING, out);
    }
}

int resp_parse(resp_parser_t *p, resp_value_t **out) {
    if (p->pos >= p->len) return 0;
    return parse_value(p, out);
}

void resp_value_free(resp_value_t *v) {
    if (!v) return;
    switch (v->type) {
        case RESP_SIMPLE_STRING:
        case RESP_ERROR:
        case RESP_BULK_STRING:
            free(v->str);
            break;
        case RESP_ARRAY:
            for (int i = 0; i < v->array.count; i++) {
                resp_value_free(v->array.elements[i]);
            }
            free(v->array.elements);
            break;
        case RESP_INTEGER:
        case RESP_NIL:
            break;
    }
    free(v);
}

/* response serialization */

void resp_buf_init(resp_buf_t *b) {
    b->cap = RESP_BUF_INIT_CAP;
    b->buf = ck_malloc(b->cap);
    b->len = 0;
}

void resp_buf_destroy(resp_buf_t *b) {
    free(b->buf);
    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
}

static void resp_buf_append(resp_buf_t *b, const char *data, size_t len) {
    if (b->len + len > b->cap) {
        while (b->len + len > b->cap) b->cap *= 2;
        b->buf = ck_realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->len, data, len);
    b->len += len;
}

static void resp_buf_printf(resp_buf_t *b, const char *fmt, ...) {
    char tmp[128];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (n > 0) {
        resp_buf_append(b, tmp, (size_t)n);
    }
}

void resp_write_simple_string(resp_buf_t *b, const char *s) {
    resp_buf_printf(b, "+%s\r\n", s);
}

void resp_write_error(resp_buf_t *b, const char *s) {
    resp_buf_printf(b, "-%s\r\n", s);
}

void resp_write_integer(resp_buf_t *b, int64_t n) {
    resp_buf_printf(b, ":%lld\r\n", (long long)n);
}

void resp_write_bulk_string(resp_buf_t *b, const char *s, size_t len) {
    resp_buf_printf(b, "$%zu\r\n", len);
    resp_buf_append(b, s, len);
    resp_buf_append(b, "\r\n", 2);
}

void resp_write_null(resp_buf_t *b) {
    resp_buf_append(b, "$-1\r\n", 5);
}

void resp_write_array_header(resp_buf_t *b, int count) {
    resp_buf_printf(b, "*%d\r\n", count);
}

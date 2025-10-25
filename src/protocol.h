#ifndef CK_PROTOCOL_H
#define CK_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    RESP_SIMPLE_STRING,
    RESP_ERROR,
    RESP_INTEGER,
    RESP_BULK_STRING,
    RESP_ARRAY,
    RESP_NIL
} resp_type_t;

typedef struct resp_value {
    resp_type_t type;
    union {
        char *str;        /* simple string, error, bulk string */
        int64_t integer;
        struct {
            struct resp_value **elements;
            int count;
        } array;
    };
} resp_value_t;

/* parser state for incremental parsing */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    size_t pos;        /* current parse position */
} resp_parser_t;

void resp_parser_init(resp_parser_t *p);
void resp_parser_destroy(resp_parser_t *p);
void resp_parser_feed(resp_parser_t *p, const char *data, size_t len);

/*
 * try to parse one complete RESP value from the buffer.
 * returns 1 on success, 0 if not enough data yet.
 * on success, consumes parsed bytes from the buffer.
 */
int resp_parse(resp_parser_t *p, resp_value_t **out);

void resp_value_free(resp_value_t *v);

/* serialization helpers - write into dynamically grown buffer */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} resp_buf_t;

void resp_buf_init(resp_buf_t *b);
void resp_buf_destroy(resp_buf_t *b);

void resp_write_simple_string(resp_buf_t *b, const char *s);
void resp_write_error(resp_buf_t *b, const char *s);
void resp_write_integer(resp_buf_t *b, int64_t n);
void resp_write_bulk_string(resp_buf_t *b, const char *s, size_t len);
void resp_write_null(resp_buf_t *b);
void resp_write_array_header(resp_buf_t *b, int count);

#endif

#include "protocol.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

static int n_fail;

static void ok(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        n_fail++;
    }
}

void test_protocol_parse_ping(void) {
    resp_parser_t p;
    resp_parser_init(&p);
    const char *msg = "*1\r\n$4\r\nPING\r\n";
    resp_parser_feed(&p, msg, strlen(msg));
    resp_value_t *v = NULL;
    ok(resp_parse(&p, &v) == 1, "parse PING array");
    ok(v != NULL && v->type == RESP_ARRAY, "type array");
    ok(v->array.count == 1, "array count 1");
    ok(v->array.elements[0]->type == RESP_BULK_STRING, "element bulk string");
    ok(v->array.elements[0]->str != NULL && strcmp(v->array.elements[0]->str, "PING") == 0, "PING string");
    resp_value_free(v);
    resp_parser_destroy(&p);
}

void test_protocol_roundtrip(void) {
    resp_buf_t b;
    resp_buf_init(&b);
    resp_write_simple_string(&b, "OK");
    ok(b.len > 0 && strstr(b.buf, "OK") != NULL, "write simple string");
    resp_write_integer(&b, 42);
    ok(strstr(b.buf, "42") != NULL, "write integer");
    resp_buf_destroy(&b);
}

void test_protocol_partial(void) {
    resp_parser_t p;
    resp_parser_init(&p);
    resp_parser_feed(&p, "*1\r\n", 4);
    resp_value_t *v = NULL;
    ok(resp_parse(&p, &v) == 0, "partial parse returns 0");
    resp_parser_feed(&p, "$4\r\nPING\r\n", 10);
    ok(resp_parse(&p, &v) == 1 && v != NULL, "complete parse");
    if (v) resp_value_free(v);
    resp_parser_destroy(&p);
}

int test_protocol_run(void) {
    n_fail = 0;
    test_protocol_parse_ping();
    test_protocol_roundtrip();
    test_protocol_partial();
    return n_fail;
}

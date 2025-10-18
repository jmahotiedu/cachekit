#include "list.h"
#include "util.h"
#include <stdlib.h>

list_t *list_create(void (*free_value)(void *)) {
    list_t *list = ck_calloc(1, sizeof(list_t));
    list->free_value = free_value;
    return list;
}

void list_destroy(list_t *list) {
    if (!list) return;
    list_node_t *node = list->head;
    while (node) {
        list_node_t *next = node->next;
        if (list->free_value && node->value) {
            list->free_value(node->value);
        }
        free(node);
        node = next;
    }
    free(list);
}

void list_lpush(list_t *list, void *value) {
    list_node_t *node = ck_malloc(sizeof(list_node_t));
    node->value = value;
    node->prev = NULL;
    node->next = list->head;

    if (list->head) {
        list->head->prev = node;
    } else {
        list->tail = node;
    }
    list->head = node;
    list->length++;
}

void list_rpush(list_t *list, void *value) {
    list_node_t *node = ck_malloc(sizeof(list_node_t));
    node->value = value;
    node->next = NULL;
    node->prev = list->tail;

    if (list->tail) {
        list->tail->next = node;
    } else {
        list->head = node;
    }
    list->tail = node;
    list->length++;
}

void *list_lpop(list_t *list) {
    if (!list->head) return NULL;

    list_node_t *node = list->head;
    void *value = node->value;

    list->head = node->next;
    if (list->head) {
        list->head->prev = NULL;
    } else {
        list->tail = NULL;
    }

    free(node);
    list->length--;
    return value;
}

void *list_rpop(list_t *list) {
    if (!list->tail) return NULL;

    list_node_t *node = list->tail;
    void *value = node->value;

    list->tail = node->prev;
    if (list->tail) {
        list->tail->next = NULL;
    } else {
        list->head = NULL;
    }

    free(node);
    list->length--;
    return value;
}

/* normalize negative index to positive */
static int normalize_index(list_t *list, int index) {
    int len = (int)list->length;
    if (index < 0) index += len;
    return index;
}

void *list_index(list_t *list, int index) {
    index = normalize_index(list, index);
    if (index < 0 || index >= (int)list->length) return NULL;

    list_node_t *node;
    /* traverse from whichever end is closer */
    if (index < (int)list->length / 2) {
        node = list->head;
        for (int i = 0; i < index; i++) node = node->next;
    } else {
        node = list->tail;
        for (int i = (int)list->length - 1; i > index; i--) node = node->prev;
    }
    return node->value;
}

int list_range(list_t *list, int start, int stop, void **out, int max_out) {
    int len = (int)list->length;

    start = normalize_index(list, start);
    stop = normalize_index(list, stop);

    /* clamp */
    if (start < 0) start = 0;
    if (stop >= len) stop = len - 1;
    if (start > stop) return 0;

    list_node_t *node = list->head;
    for (int i = 0; i < start; i++) node = node->next;

    int count = 0;
    for (int i = start; i <= stop && count < max_out; i++) {
        out[count++] = node->value;
        node = node->next;
    }
    return count;
}

size_t list_length(list_t *list) {
    return list->length;
}

void list_move_to_head(list_t *list, list_node_t *node) {
    if (node == list->head) return;

    /* detach */
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;
    if (node == list->tail) list->tail = node->prev;

    /* reinsert at head */
    node->prev = NULL;
    node->next = list->head;
    if (list->head) list->head->prev = node;
    list->head = node;
}

void list_remove_node(list_t *list, list_node_t *node) {
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }

    list->length--;
    free(node);
}

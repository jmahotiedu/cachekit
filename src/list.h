#ifndef CK_LIST_H
#define CK_LIST_H

#include <stddef.h>

typedef struct list_node {
    struct list_node *prev;
    struct list_node *next;
    void *value;
} list_node_t;

typedef struct {
    list_node_t *head;
    list_node_t *tail;
    size_t length;
    void (*free_value)(void *);
} list_t;

list_t *list_create(void (*free_value)(void *));
void list_destroy(list_t *list);

void list_lpush(list_t *list, void *value);
void list_rpush(list_t *list, void *value);
void *list_lpop(list_t *list);
void *list_rpop(list_t *list);

/* 0-based index, negative indexes from tail (-1 = last) */
void *list_index(list_t *list, int index);

/* range retrieval: writes up to max_out pointers into out[], returns count */
int list_range(list_t *list, int start, int stop, void **out, int max_out);

size_t list_length(list_t *list);

/* move a node to the head (for LRU) */
void list_move_to_head(list_t *list, list_node_t *node);

/* remove a specific node */
void list_remove_node(list_t *list, list_node_t *node);

#endif

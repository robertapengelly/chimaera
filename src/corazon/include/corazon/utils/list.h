/******************************************************************************
 * @file            list.h
 *****************************************************************************/
#ifndef     _LIST_H
#define     _LIST_H

#include    <stddef.h>
#include    <stdint.h>

typedef struct list_node {

    struct list_node *next;
    void *value;

} list_node_t;

typedef struct list {

    struct list_node *head;
    size_t length;

} list_t;

list_t *list_create (void);

struct list_node *list_create_node (void *value, struct list_node *next);
struct list_node *list_get_node (list_t *list, size_t index);

void list_insert (list_t *list, size_t index, void *value);
void list_append (list_t *list, void *value);

void *list_get (list_t *list, size_t index);

void list_remove_index (list_t *list, int index);
void list_remove (list_t *list, list_node_t *node);

int list_find (list_t *list, list_node_t *node);

void list_destruct (list_t *list);
void list_construct (list_t *list);

void list_free (list_t *list);
void list_pop (list_t * list);

#endif      /* _LIST_H */

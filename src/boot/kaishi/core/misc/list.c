/******************************************************************************
 * @file            list.c
 *****************************************************************************/
#include    <kaishi/list.h>
#include    <kaishi/stdlib.h>

#include    <stddef.h>

list_t *list_create (void) {
    return kmalloc (sizeof (list_t));
}

/**
 * Creates a node with the given nullable value and returns the result.
 * Should be freed on removal from the list.
 */
struct list_node *list_create_node (void *value, struct list_node *next) {

    struct list_node *node = kmalloc (sizeof (struct list_node));
    
    node->value = value;
    node->next = next;
    
    return node;

}

/** Returns the node at the given index. */
struct list_node *list_get_node (list_t *list, size_t index) {

    struct list_node *current;
    int i;
    
    if (index < 0 || index >= list->length) {
        return 0;
    }
    
    current = list->head;
    
    for (i = 1; i <= index && current; i++) {
        current = current->next;
    }
    
    return current;

}

/** Adds a value to the end of the given list. */
void list_append (list_t *list, void *value) {

    struct list_node *node = list_create_node (value, 0);
    
    if (list->length) {
    
        struct list_node *last = list_get_node (list, list->length - 1);
        last->next = node;
    
    } else {
        list->head = node;
    }
    
    list->length++;

}

/** Inserts a newly created node from the given value at the given index. */
void list_insert (list_t *list, size_t index, void *value) {

    if (index == list->length) {
    
        list_append (list, value);
        return;
    
    }
    
    if (!index) {
    
        struct list_node **head = &list->head;
        
        *head = list_create_node (value, *head);
        list->length++;
    
    } else {
    
        struct list_node *prev = list_get_node (list, index - 1);
        prev->next = list_create_node (value, prev->next);
        
        list->length++;
    
    }

}

/** Returns the value of the node at the given index. */
void *list_get (list_t *list, size_t index) {

    struct list_node *node = list_get_node (list, index);
    return node ? node->value : 0;

}

/******************************************************************************
 * @file            list.c
 *****************************************************************************/
#include    <corazon/utils/list.h>

#include    <stddef.h>
#include    <string.h>

extern void *kmalloc (size_t size);
extern void kfree (void *ptr);

list_t *list_create (void) {

    list_t *list = kmalloc (sizeof (list_t));
    
    if (list) {
        memset (list, 0, sizeof (*list));
    }
    
    return list;

}

/**
 * Creates a node with the given nullable value and returns the result.
 * Should be freed on removal from the list.
 */
struct list_node *list_create_node (void *value, struct list_node *next) {

    struct list_node *node = kmalloc (sizeof (struct list_node));
    
    if (node) {
    
        node->value = value;
        node->next = next;
    
    }
    
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

/** Removes and frees the first node in the list. */
void list_pop (list_t * list) {

    list_node_t *next_node, **head;
    
    if (!list->length) {
        return;
    }
    
    next_node = NULL;
    head = &list->head;
    
    next_node = (*head)->next;
    
    if ((*head)->value != NULL) {
        kfree ((*head)->value);
    }
    
    kfree (*head);
    
    *head = next_node;
    list->length--;

}

/** Returns the value of the node at the given index. */
void *list_get (list_t *list, size_t index) {

    struct list_node *node = list_get_node (list, index);
    return node ? node->value : 0;

}

/** Find the index of a give node. */
int list_find (list_t *list, list_node_t *node) {

    list_node_t *current;
    int index;
    
    current = list->head;
    
    for (index = 0; current; index++) {
    
        if (current == node) {
            return index;
        }
        
        current = current->next;
    }
    
    return -1;

}

/** Removes and frees the node at the given index. */
void list_remove_index (list_t *list, int index) {

    list_node_t *current, *temp_node;
    int i;
    
    if (list->length == 0) {
        return;
    }
    
    if (!index) {
    
        list_pop (list);
        return;
    
    }
    
    current = list->head;
    temp_node = NULL;
    
    for (i = 1; i < index && current != NULL ; i++) {
        current = current->next;
    }
    
    temp_node = current->next;
    current->next = temp_node->next;
    
    if(temp_node->value != NULL) {
        kfree (temp_node->value);
    }
    
    kfree (temp_node);
    list->length--;

}

void list_remove (list_t *list, list_node_t *node) {

    int index = list_find (list, node);
    
    if (index == -1) {
        return;
    }
    
    list_remove_index (list, index);

}

/** Destroy the provided list. */
void list_destruct (list_t *list) {

    list_node_t *current;
    list_node_t *next;
    
    current = list->head;
    
    while (current) {
    
        next = current->next;
        kfree (current);
        
        current = next;
    
    }
    
    list->head = 0;
    list->length = 0;

}

/** Make sure the provided list is empty. */
void list_construct (list_t *list) {

    list->head = 0;
    list->length = 0;

}

/** Free everything related to the provided list. */
void list_free (list_t *list) {

    list_destruct (list);
    kfree (list);

}

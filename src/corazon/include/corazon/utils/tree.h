/******************************************************************************
 * @file            tree.h
 *****************************************************************************/
#ifndef     _TREE_H
#define     _TREE_H

#include    <corazon/utils/list.h>
#include    <stddef.h>

struct tree_node {

    void *value;
    
    list_t *children;
    struct tree_node *parent;

};

typedef struct {

    size_t nodes;
    struct tree_node *root;

} tree_t;

struct tree_node *tree_node_create (void *value);
tree_t *tree_create (void);

struct tree_node *tree_node_insert_child(tree_t *tree, struct tree_node *parent, void *value);
void tree_node_insert_child_node (tree_t *tree, struct tree_node *parent, struct tree_node *node);

void tree_set_root (tree_t *tree, void *value);

#endif      /* _TREE_H */

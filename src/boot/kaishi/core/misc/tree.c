/******************************************************************************
 * @file            tree.c
 *****************************************************************************/
#include    <kaishi/stdlib.h>
#include    <kaishi/tree.h>

tree_t *tree_create (void) {
    return kmalloc (sizeof (tree_t));
}

struct tree_node *tree_node_create (void *value) {

    struct tree_node *node = kmalloc (sizeof (struct tree_node));
    
    node->children = list_create ();
    node->value = value;
    
    return node;

}

struct tree_node *tree_node_insert_child(tree_t *tree, struct tree_node *parent, void *value) {

    struct tree_node *out = tree_node_create (value);
    tree_node_insert_child_node (tree, parent, out);
    
    return out;

}

void tree_node_insert_child_node (tree_t *tree, struct tree_node *parent, struct tree_node *node) {

    list_append (parent->children, node);
    
    node->parent = parent;
    tree->nodes++;

}

void tree_set_root (tree_t *tree, void *value) {

    struct tree_node *root = tree_node_create (value);
    
    tree->root = root;
    tree->nodes = 1;

}

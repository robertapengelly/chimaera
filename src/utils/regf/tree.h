/******************************************************************************
 * @file            tree.h
 *****************************************************************************/
#ifndef     _TREE_H
#define     _TREE_H

#include    "regf.h"

const char *canonical_value_name (const char *name);
void stats_collect (rf_key_t *k, uint32_t depth, stats_t *s);

value_t *find_value (value_t *head, const char *name);
rf_key_t *append_child (rf_key_t *parent, const char *name);

void add_value_node (rf_key_t *k, value_t *v);
value_t *value_new (const char *name, uint32_t type, const void *data, uint32_t data_len);

rf_key_t *collapse_synthetic_root (rf_key_t *root);
rf_key_t *add_key_path (rf_key_t *root, char **p);
rf_key_t *key_new (const char *name, rf_key_t *parent);

void add_binary (rf_key_t *root, const char *start, const char *path, const char *name, const char *caret, const void *data, uint32_t data_size);

void add_dword (rf_key_t *root, const char *start, const char *path, const char *name, const char *caret, uint32_t dword);
void add_qword (rf_key_t *root, const char *start, const char *path, const char *name, const char *caret, uint64_t qword);

void add_text_value (rf_key_t *root, const char *start, const char *path, const char *name, char **pp, uint32_t type);

uint32_t count_values (rf_key_t *k);
uint32_t count_children (rf_key_t *k);

void sort_children (rf_key_t *parent);

uint32_t max_subkey_name_u16 (rf_key_t *k);
uint32_t max_value_name_u16 (rf_key_t *k);

uint32_t max_value_data_size (rf_key_t *k);

#endif      /* _TREE_H */

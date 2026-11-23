/******************************************************************************
 * @file            ll.h
 *****************************************************************************/
#ifndef     _LL_H
#define     _LL_H

#include    <kaishi/vfs/node.h>
#include    <stdint.h>

int load_line (char **line_p, char **line_end_p, uint64_t *newlines_p, struct fs_node *node, void **load_line_internal_data_p);

void load_line_destroy_internal_data (void *load_line_internal_data);
void *load_line_create_internal_data (unsigned long *new_line_number_p);

#endif      /* _LL_H */

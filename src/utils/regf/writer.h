/******************************************************************************
 * @file            writer.h
 *****************************************************************************/
#ifndef     _WRITER_H
#define     _WRITER_H

#include    "regf.h"

int write_hive_file (const char *ofile, rf_key_t *root);
void validate_tree_rec (const rf_key_t *k);

#endif      /* _WRITER_H */

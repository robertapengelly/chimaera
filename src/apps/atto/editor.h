/******************************************************************************
 * @file            editor.h
 *****************************************************************************/
#ifndef     _EDITOR_H
#define     _EDITOR_H

#include    "types.h"

extern editor_state_t g_editor;
void editor_init (void);

void editor_insert_row (size_t at, const char *str, size_t len);
void editor_run ();

#endif      /* _EDITOR_H */

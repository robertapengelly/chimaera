/******************************************************************************
 * @file            buffer.h
 *****************************************************************************/
#ifndef     _BUFFER_H
#define     _BUFFER_H

#include    "types.h"

void buffer_append (append_buffer_t *ab, const char *str, size_t len);
void buffer_free (append_buffer_t *ab);

#endif      /* _BUFFER_H */

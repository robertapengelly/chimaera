/******************************************************************************
 * @file            stdlib.h
 *****************************************************************************/
#ifndef     _KAISHI_STDLIB_H
#define     _KAISHI_STDLIB_H

void kfree (void *ptr);

#include    <stddef.h>

void *kmalloc (size_t size);
void *krealloc (void *ptr, size_t size);

#endif      /* _KAISHI_STDLIB_H */

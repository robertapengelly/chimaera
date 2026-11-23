/******************************************************************************
 * @file            string.h
 *****************************************************************************/
#ifndef     _STRING_H
#define     _STRING_H

#define     __NEED_SIZE_T
#define     __NEED_NULL
#include    <stddef.h>

char *strcpy (char *dst, const char *src);

int strncmp (const char *s1, const char *s2, size_t len);
int strcmp (const char *s1, const char *s2);

int memcmp (const void *s1, const void *s2, size_t n);
void *memmove (void *dest, const void *src, size_t n);

size_t strlen (const char *s);

void *memcpy (void *dest, void *src, size_t cnt);
void *memset (void* mem, int value, size_t len);

#endif      /* _STRING_H */

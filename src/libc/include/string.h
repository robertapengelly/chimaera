/******************************************************************************
 * @file            string.h
 *****************************************************************************/
#ifndef     _STRING_H
#define     _STRING_H

#define     __NEED_SIZE_T
#define     __NEED_NULL
#include    <stddef.h>

char *strndup (const char *s, size_t n);
char *strdup (const char *s);

char *strrchr (const char *s, int c);
char *strchr (const char *s, int c);

char *strncpy (char *dest, const char *src, size_t len);
char *strcpy (char *dest, const char *src);
char *strcat (char *dest, const char *src);

int strcasecmp (const char *s1, const char *s2);
int strncmp (const char *s1, const char *s2, size_t len);
int strcmp (const char *s1, const char *s2);

int memcmp (const void *s1, const void *s2, size_t n);
void *memmove (void *dest, const void *src, size_t n);

char *strpbrk (const char *str, const char *accept);
size_t strlen (const char *s);

void *memcpy (void *dest, void *src, size_t cnt);
void *memset (void* mem, int value, size_t len);

size_t strcspn (const char *s, const char *reject);
size_t strspn (const char *s, const char *accept);

#endif      /* _STRING_H */

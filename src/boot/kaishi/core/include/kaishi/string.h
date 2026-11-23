/******************************************************************************
 * @file            string.h
 *****************************************************************************/
#ifndef     _KAISHI_STRING_H
#define     _KAISHI_STRING_H

char *kstrdup (const char *s);

#include    <stddef.h>
char *kstrndup (const char *s, size_t len);

#endif      /* _KAISHI_STRING_H */

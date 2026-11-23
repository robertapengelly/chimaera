/******************************************************************************
 * @file            stdlib.h
 *****************************************************************************/
#ifndef     _STDLIB_H
#define     _STDLIB_H

#define     __NEED_SIZE_T
#define     __NEED_NULL
#include    <stddef.h>

void *realloc (void *ptr, size_t size);
void *malloc (size_t __size);
void *calloc (size_t nmemb, size_t size);

int atexit (void (*func) (void));
void free (void *__ptr);

void exit (int status) __attribute__ ((noreturn));
void _Exit (int status) __attribute__ ((noreturn));

char *getenv (const char *name);
int setenv (const char *name, const char *value, int overwrite);

double strtod (const char *nptr, char **endptr);
long strtol (const char *nptr, char **endptr, int base);
unsigned long strtoul (const char *nptr, char **endptr, int base);

void abort (void) __attribute__ ((noreturn));

#endif      /* _STDLIB_H */

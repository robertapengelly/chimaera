/******************************************************************************
 * @file            lib.h
 *****************************************************************************/
#ifndef     _LIB_H
#define     _LIB_H

char *xstrdup (const char *str);
int wild_compare (const char *wild, const char *s);

void *xmalloc (unsigned long size);
void *xrealloc (void *ptr, unsigned long size);

void parse_args (int argc, char **argv, int optind);

#include    <stdint.h>
int lsb (uint64_t x, uint64_t n);

uint64_t array_to_integer (unsigned char *arr, int size, int bigendian);
uint64_t round_up (uint64_t x, uint64_t m);

#endif      /* _LIB_H */

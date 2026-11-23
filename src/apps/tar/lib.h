/******************************************************************************
 * @file            lib.h
 *****************************************************************************/
#ifndef     _LIB_H
#define     _LIB_H

char *xstrdup (const char *__str);

void dynarray_add (void *ptab, long *nb_ptr, void *data);
void parse_args (int argc, char **argv, int optind);

void *xmalloc (unsigned long __size);
void *xrealloc (void *__ptr, unsigned long __size);

#endif      /* _LIB_H */

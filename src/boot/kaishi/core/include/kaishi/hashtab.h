/******************************************************************************
 * @file            hashtab.h
 *****************************************************************************/
#ifndef     _HASHTAB_H
#define     _HASHTAB_H

#include    <stdint.h>

struct hashtab_name {

    const char *chars;
    uint32_t bytes, hash;

};

struct hashtab_entry {

    struct hashtab_name *key;
    void *value;

};

struct hashtab {

    struct hashtab_entry *entries;
    int capacity, count, used;

};

struct hashtab_name *hashtab_alloc_name (const char *str);
struct hashtab_name *hashtab_get_key (struct hashtab *table, const char *name);

int hashtab_put (struct hashtab *table, struct hashtab_name *key, void *value);
void *hashtab_get (struct hashtab *table, struct hashtab_name *key);

void hashtab_remove (struct hashtab *table, struct hashtab_name *key);

#endif      /* _HASHTAB_H */

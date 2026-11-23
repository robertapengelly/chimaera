/******************************************************************************
 * @file            hashtab.c
 *****************************************************************************/
#include    <kaishi/hashtab.h>
#include    <kaishi/stdlib.h>
#include    <stddef.h>
#include    <stdint.h>
#include    <string.h>

static struct hashtab_entry *find_entry (struct hashtab_entry *entries, uint32_t capacity, struct hashtab_name *key);

static int adjust_capacity (struct hashtab *table, uint32_t new_capacity) {

    struct hashtab_entry *new_entries, *old_entries;
    uint32_t i, new_count, old_capacity;
    
    if ((new_entries = kmalloc (sizeof (*new_entries) * new_capacity)) == NULL) {
        return -2;
    }
    
    for (i = 0; i < new_capacity; i++) {
    
        struct hashtab_entry *entry = &new_entries[i];
        
        entry->key = NULL;
        entry->value = NULL;
    
    }
    
    old_entries = table->entries;
    old_capacity = table->capacity;
    
    new_count = 0;
    
    for (i = 0; i < old_capacity; i++) {
    
        struct hashtab_entry *entry = &old_entries[i], *dest;
        
        if (entry->key == NULL) {
            continue;
        }
        
        dest = find_entry (new_entries, new_capacity, entry->key);
        
        dest->key = entry->key;
        dest->value = entry->value;
        
        new_count++;
    
    }
    
    kfree (old_entries);
    
    table->capacity = new_capacity;
    table->count = new_count;
    table->entries = new_entries;
    table->used = new_count;
    
    return 0;

}

static struct hashtab_entry *find_entry (struct hashtab_entry *entries, uint32_t capacity, struct hashtab_name *key) {

    struct hashtab_entry *tombstone = NULL;
    uint32_t index;
    
    for (index = key->hash % capacity; ; index = (index + 1) % capacity) {
    
        struct hashtab_entry *entry = &entries[index];
        
        if (entry->key == NULL) {
        
            if (entry->value == NULL) {
            
                if (tombstone == NULL) {
                    return entry;
                }
                
                return tombstone;
            
            } else if (tombstone == NULL) {
                tombstone = entry;
            }
        
        } else if (entry->key->bytes == key->bytes) {
        
            if (memcmp (entry->key->chars, key->chars, key->bytes) == 0 && entry->key->hash == key->hash) {
                return entry;
            }
        
        }
    
    }

}

static uint32_t hash_string (const void *p, uint32_t length) {

    uint8_t *str = (uint8_t *) p;
    uint32_t i;
    
    uint32_t result = 0;
    
    for (i = 0; i < length; i++) {
        result = (((uint16_t) str[i]) << 4) + (result >> 9) + result + (result >> 3) + (((uint16_t) str[i]) << 2) - (result << 12);
    }
    
    return result;

}

struct hashtab_name *hashtab_alloc_name (const char *str) {

    uint32_t bytes = strlen (str), hash = hash_string (str, bytes);
    struct hashtab_name *name;
    
    if ((name = kmalloc (sizeof (*name))) == NULL) {
        return NULL;
    }
    
    name->bytes = bytes;
    name->chars = str;
    name->hash = hash;
    
    return name;

}

struct hashtab_name *hashtab_get_key (struct hashtab *table, const char *name) {

    struct hashtab_name *key;
    struct hashtab_entry *entry;
    
    if (table == NULL || table->count == 0 || !(key = hashtab_alloc_name (name))) {
        return 0;
    }
    
    entry = find_entry (table->entries, table->capacity, key);
    kfree (key);
    
    return entry->key;

}

void *hashtab_get (struct hashtab *table, struct hashtab_name *key) {

    struct hashtab_entry *entry;
    
    if (table == NULL || table->count == 0) {
        return NULL;
    }
    
    entry = find_entry (table->entries, table->capacity, key);
    
    if (entry->key == NULL) {
        return NULL;
    }
    
    return entry->value;

}

int hashtab_put (struct hashtab *table, struct hashtab_name *key, void *value) {

    const int MIN_CAPACITY = 15;
    
    struct hashtab_entry *entry;
    int ret = 0;
    
    if (table->used >= table->capacity / 2) {
    
        int capacity = table->capacity * 2 - 1;
        
        if (capacity < MIN_CAPACITY) {
            capacity = MIN_CAPACITY;
        }
        
        if ((ret = adjust_capacity (table, capacity))) {
            return ret;
        }
    
    }
    
    entry = find_entry (table->entries, table->capacity, key);
    
    if (entry->key == NULL) {
    
        table->count++;
        
        if (entry->value == NULL) {
            table->used++;
        }
    
    }
    
    entry->key = key;
    entry->value = value;
    
    return 0;

}

void hashtab_remove (struct hashtab *table, struct hashtab_name *key) {

    struct hashtab_entry *entry;
    
    if ((entry = find_entry (table->entries, table->capacity, key)) != NULL) {
    
        entry->key = NULL;
        entry->value = NULL;
        
        --table->count;
    
    }

}

/******************************************************************************
 * @file            environ.c
 *****************************************************************************/
//#include    <errno.h>
#include    <stddef.h>
#include    <stdlib.h>
#include    <string.h>

struct environ_owned_string {

    char *string;
    struct environ_owned_string *next;

};

static char *empty_environ[] = { NULL };
static char **allocated_environ = NULL;

static struct environ_owned_string *allocated_strings = NULL;
char **__environ = empty_environ;

char *getenv (const char *name) {

    size_t length, i;
    
    if (!name || !*name || strchr (name, '=')) {
        return NULL;
    }
    
    length = strlen (name);
    
    for (i = 0; __environ[i]; i++) {
    
        if (strncmp (__environ[i], name, length) == 0 && __environ[i][length] == '=') {
            return __environ[i] + length + 1;
        }
    
    }
    
    return NULL;

}

int setenv (const char *name, const char *value, int overwrite) {

    struct environ_owned_string *owned, **link;
    
    char **entries;
    char *string, *entry;
    
    size_t name_length, value_length, count, index;
    
    if (!name || !*name || strchr (name, '=') || !value) {
    
        //errno = EINVAL;
        return -1;
    
    }
    
    name_length = strlen (name);
    value_length = strlen (value);
    
    index = (size_t) -1;
    
    for (count = 0; __environ[count]; count++) {
    
        entry = __environ[count];
        
        if (index == (size_t) -1 && strncmp (entry, name, name_length) == 0 && *(entry + name_length) == '=') {
            index = count;
        }
    
    }
    
    if (index != (size_t) -1 && !overwrite) {
        return 0;
    }
    
    if (name_length > (size_t) -1 - value_length - 2 || count >= (size_t) -1 / sizeof (*entries) - 1) {
    
        //errno = ENOMEM;
        return -1;
    
    }
    
    if (!(string = malloc (name_length + value_length + 2))) {
    
        //errno = ENOMEM;
        return -1;
    
    }
    
    memcpy (string, name, name_length);
    string[name_length] = '=';
    
    memcpy (string + name_length + 1, value, value_length + 1);
    
    if (!(owned = malloc (sizeof (*owned)))) {
    
        free (string);
        
        //errno = ENOMEM;
        return -1;
    
    }
    
    if (!(entries = malloc ((count + 2) * sizeof (*entries)))) {
    
        free (owned);
        free (string);
        
        //errno = ENOMEM;
        return -1;
    
    }
    
    memcpy (entries, __environ, count * sizeof (*entries));
    
    if (index == (size_t) -1) {
    
        index = count;
        entries[count + 1] = NULL;
    
    } else {
    
        for (link = &allocated_strings; *link; link = &(*link)->next) {
        
            if ((*link)->string == entries[index]) {
            
                struct environ_owned_string *old = *link;
                *link = old->next;
                
                free (old->string);
                free (old);
                
                break;
            
            }
        
        }
        
        entries[count] = NULL;
    
    }
    
    entries[index] = string;
    
    owned->string = string;
    owned->next = allocated_strings;
    
    allocated_strings = owned;
    
    if (__environ == allocated_environ) {
        free (allocated_environ);
    }
    
    allocated_environ = entries;
    __environ = entries;
    
    return 0;

}

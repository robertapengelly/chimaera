/******************************************************************************
 * @file            realloc.c
 *****************************************************************************/
#include    <stdlib.h>
#include    <string.h>

__attribute__ ((dllexport)) void *realloc (void *ptr, size_t size) {

    void *new_ptr;
    size_t old_size;
    
    if (!ptr) {
        return malloc (size);
    }
    
    if (!size) {
    
        free (ptr);
        return NULL;
    
    }
    
    if (size > (size_t) -1 - sizeof (size_t)) {
        return NULL;
    }
    
    old_size = *((size_t *) ptr - 1);
    
    if (size <= old_size) {
        return ptr;
    }
    
    new_ptr = malloc (size);
    
    if (!new_ptr) {
        return NULL;
    }
    
    memcpy (new_ptr, ptr, old_size);
    free (ptr);
    
    return new_ptr;

}

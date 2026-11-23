/******************************************************************************
 * @file            calloc.c
 *****************************************************************************/
#include    <errno.h>
#include    <stdlib.h>
#include    <string.h>

__attribute__ ((dllexport)) void *calloc (size_t nmemb, size_t size) {

    size_t total;
    void *ptr;
    
    if (nmemb && size > ((size_t) -1 - sizeof (size_t)) / nmemb) {
    
        errno = ENOMEM;
        return NULL;
    
    }
    
    total = nmemb * size;
    ptr = malloc (total);
    
    if (ptr) {
        memset (ptr, 0, total);
    }
    
    return ptr;

}

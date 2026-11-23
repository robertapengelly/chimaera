/******************************************************************************
 * @file            vector.c
 *****************************************************************************/
#include    <kaishi/stdlib.h>
#include    <kaishi/vector.h>
#include    <stddef.h>
#include    <stdint.h>

int vec_adjust (struct vector *vec, int length) {

    if (vec->capacity <= length) {
    
        if (vec->capacity == 0) {
            vec->capacity = 16;
        } else {
            vec->capacity <<= 1;
        }
        
        vec->data = krealloc (vec->data, sizeof (*(vec->data)) * vec->capacity);
    
    }
    
    return 0;

}

void *vec_pop (struct vector *vec) {

    if (!vec || vec == NULL) {
        return NULL;
    }
    
    if (vec->length == 0) {
        return NULL;
    }
    
    return vec->data[--vec->length];

}

int vec_push (struct vector *vec, void *elem) {

    int ret;
    
    if ((ret = vec_adjust (vec, vec->length)) != 0) {
        return ret;
    }
    
    vec->data[vec->length++] = elem;
    return 0;

}

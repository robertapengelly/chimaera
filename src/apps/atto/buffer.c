/******************************************************************************
 * @file            buffer.c
 *****************************************************************************/
#include    <stdlib.h>
#include    <string.h>

#include    "buffer.h"

void buffer_append (append_buffer_t *ab, const char *str, size_t len) {

    char *new_buffer;
    
    if (!ab || !str) {
        return;
    }
    
    if (!(new_buffer = realloc (ab->buffer, ab->length + len))) {
        return;
    }
    
    memcpy (&new_buffer[ab->length], str, len);
    
    ab->buffer = new_buffer;
    ab->length += len;

}

void buffer_free (append_buffer_t *ab) {

    if (ab) {
    
        free (ab->buffer);
        
        ab->buffer = 0;
        ab->length = 0;
    
    }

}

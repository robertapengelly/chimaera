/******************************************************************************
 * @file            fnv1a.c
 *****************************************************************************/
#include    <stddef.h>
#include    <stdint.h>

uint64_t checksum64_fnv1a (const void *data, size_t len) {

    const uint8_t *p = (const uint8_t *) data;
    uint64_t h = UINT64_C (14695981039346656037);           /* FNV offset basis */
    
    while (len--) {
    
        h ^= *p++;
        h *= UINT64_C (1099511628211);                      /* FNV prime */
    
    }
    
    return h;

}

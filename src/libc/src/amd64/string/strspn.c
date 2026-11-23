/******************************************************************************
 * @file            strspn.c
 *****************************************************************************/
#include    <string.h>

size_t strspn (const char *s, const char *accept) {

    size_t n = 0;
    
    while (*s && strchr (accept, (int) (unsigned char) *s) != NULL) {
    
        s++;
        n++;
    
    }
    
    return n;

}

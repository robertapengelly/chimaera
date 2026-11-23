/******************************************************************************
 * @file            strcspn.c
 *****************************************************************************/
#include    <string.h>

__attribute__ ((dllexport)) size_t strcspn (const char *s, const char *reject) {

    size_t n = 0;
    
    while (*s && strchr (reject, (int) (unsigned char) *s) == NULL) {
    
        s++;
        n++;
    
    }
    
    return n;

}

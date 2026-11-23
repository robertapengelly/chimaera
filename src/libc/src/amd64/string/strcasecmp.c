/******************************************************************************
+ * @file            strcasecmp.c
 *****************************************************************************/
#include    <strings.h>

int strcasecmp (const char *s1, const char *s2) {

    int c1, c2;
    
    for (;;) {
    
        c1 = (unsigned char) *s1++;
        c2 = (unsigned char) *s2++;
        
        if (c1 >= 'A' && c1 <= 'Z') {
            c1 |= 0x20;
        }
        
        if (c2 >= 'A' && c2 <= 'Z') {
            c2 |= 0x20;
        }
        
        if (c1 != c2 || !c1) {
            return c1 - c2;
        }
    
    }

}

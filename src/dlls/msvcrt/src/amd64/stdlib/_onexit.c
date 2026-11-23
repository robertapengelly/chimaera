/******************************************************************************
 * @file            _onexit.c
 *****************************************************************************/
#include    <stdlib.h>

typedef int (*onexit_t) (void);

__attribute__ ((dllexport)) onexit_t _onexit (onexit_t func) {

    if (!func || atexit ((void (*) (void)) func)) {
        return NULL;
    }
    
    return func;

}

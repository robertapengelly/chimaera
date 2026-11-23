/******************************************************************************
 * @file            __dllonexit.c
 *****************************************************************************/
#include    <stddef.h>
#include    <stdlib.h>

typedef int (*onexit_t) (void);

__attribute__ ((dllexport)) onexit_t __dllonexit (onexit_t func, onexit_t **pbegin, onexit_t **pend) {

    onexit_t *handlers;
    size_t count;
    
    if (!func || !pbegin || !pend) {
        return NULL;
    }
    
    if (!*pbegin && !*pend) {
        count = 0;
    } else {
    
        if (!*pbegin || !*pend || *pend < *pbegin) {
            return NULL;
        }
        
        count = *pend - *pbegin;
    
    }
    
    if (count >= (size_t) -1 / sizeof (*handlers)) {
        return NULL;
    }
    
    handlers = realloc (*pbegin, (count + 1) * sizeof (*handlers));
    
    if (!handlers) {
        return NULL;
    }
    
    handlers[count] = func;
    
    *pbegin = handlers;
    *pend = handlers + count + 1;
    
    return func;

}

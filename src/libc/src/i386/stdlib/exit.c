/******************************************************************************
 * @file            exit.c
 *****************************************************************************/
#include    <stdlib.h>
#include    <unistd.h>

#define     ATEXIT_MAX_FUNCS             32

static void (*atexit_handlers[ATEXIT_MAX_FUNCS]) (void);
static int atexit_count = 0;

int atexit (void (*func) (void)) {

    if (!func || atexit_count >= ATEXIT_MAX_FUNCS) {
        return -1;
    }
    
    atexit_handlers[atexit_count++] = func;
    return 0;

}

void exit (int status) {

    while (atexit_count) {
    
        void (*func) (void) = atexit_handlers[--atexit_count];
        func ();
    
    }
    
    _Exit (status);

}

void _Exit (int status) {
    _exit (status);
}

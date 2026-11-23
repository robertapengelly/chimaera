/******************************************************************************
 * @file            exit.c
 *****************************************************************************/
#include    <stdint.h>
#include    <stdlib.h>

#define     ATEXIT_MAX_FUNCS             32
__attribute__ ((dllimport, ms_abi)) int32_t ZwTerminateProcess (void *ProcessHandle, uint32_t ExitStatus);

void _Exit (int status) __attribute__ ((noreturn));
void _exit (int status) __attribute__ ((noreturn));

static void (*atexit_handlers[ATEXIT_MAX_FUNCS]) (void);
static int atexit_count = 0;

__attribute__ ((dllexport)) int atexit (void (*func) (void)) {

    if (!func || atexit_count >= ATEXIT_MAX_FUNCS) {
        return -1;
    }
    
    atexit_handlers[atexit_count++] = func;
    return 0;

}

__attribute__ ((dllexport, noreturn)) void exit (int status) {

    while (atexit_count) {
    
        void (*func) (void) = atexit_handlers[--atexit_count];
        func ();
    
    }
    
    _Exit (status);

}

__attribute__ ((dllexport, noreturn)) void _Exit (int status) {
    _exit (status);
}

__attribute__ ((dllexport, noreturn)) void _exit (int status) {

    ZwTerminateProcess ((void *) (uintptr_t) -1, (uint32_t) status);
    __builtin_unreachable ();

}

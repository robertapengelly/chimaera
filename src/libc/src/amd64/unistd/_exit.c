/******************************************************************************
 * @file            _exit.c
 *****************************************************************************/
#include    <unistd.h>

void _exit (int status) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x3C), "D" (status) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x01), "b" (status) : "memory");
#endif
    
    __builtin_unreachable ();

}

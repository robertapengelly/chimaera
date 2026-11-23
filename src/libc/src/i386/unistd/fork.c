/******************************************************************************
 * @file            fork.c
 *****************************************************************************/
#include    <errno.h>
#include    <unistd.h>

int fork (void) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x39) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x02));
#endif

    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

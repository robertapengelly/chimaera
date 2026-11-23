/******************************************************************************
 * @file            dup.c
 *****************************************************************************/
#include    <errno.h>
#include    <unistd.h>

int dup (int __fd) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x20), "D" (__fd) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x29), "b" (__fd));
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

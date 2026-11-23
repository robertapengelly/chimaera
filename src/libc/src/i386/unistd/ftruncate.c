/******************************************************************************
 * @file            ftruncate.c
 *****************************************************************************/
#include    <errno.h>
#include    <stdint.h>
#include    <unistd.h>

int ftruncate (int __fd, off_t __length) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x4D), "D" (__fd), "S" ((intptr_t) __length) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x5D), "b" (__fd), "c" (__length) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

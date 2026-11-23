/******************************************************************************
 * @file            close.c
 *****************************************************************************/
#include    <errno.h>
#include    <unistd.h>

int close (int __fd) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x03), "D" (__fd) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x06), "b" (__fd));
#endif

    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

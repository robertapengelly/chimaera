/******************************************************************************
 * @file            dup2.c
 *****************************************************************************/
#include    <errno.h>
#include    <unistd.h>

int dup2 (int __fd, int __newfd) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x21), "D" (__fd), "S" (__newfd) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x3F), "b" (__fd), "c" (__newfd));
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

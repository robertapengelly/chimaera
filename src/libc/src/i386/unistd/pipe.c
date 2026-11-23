/******************************************************************************
 * @file            pipe.c
 *****************************************************************************/
#include    <errno.h>
#include    <unistd.h>

int pipe (int __fds[2]) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x16), "D" (__fds) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x2A), "b" (__fds) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

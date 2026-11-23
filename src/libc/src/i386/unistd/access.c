/******************************************************************************
 * @file            access.c
 *****************************************************************************/
#include    <errno.h>
#include    <unistd.h>

int access (const char *__path, int __mode) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x15), "D" (__path), "S" (__mode) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x21), "b" (__path), "c" (__mode) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

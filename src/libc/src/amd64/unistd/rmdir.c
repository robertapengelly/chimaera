/******************************************************************************
 * @file            rmdir.c
 *****************************************************************************/
#include    <errno.h>
#include    <unistd.h>

int rmdir (const char *__path) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x54), "D" (__path) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x28), "b" (__path) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

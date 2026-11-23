/******************************************************************************
 * @file            chdir.c
 *****************************************************************************/
#include    <errno.h>
#include    <unistd.h>

int chdir (const char *__path) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x50), "D" (__path) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x0C), "b" (__path) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

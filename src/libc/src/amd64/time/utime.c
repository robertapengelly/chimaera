/******************************************************************************
 * @file            utime.c
 *****************************************************************************/
#include    <errno.h>
#include    <utime.h>

int utime (const char *__path, const struct utimbuf *__times) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x84), "D" (__path), "S" (__times) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x1E), "b" (__path), "c" (__times) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

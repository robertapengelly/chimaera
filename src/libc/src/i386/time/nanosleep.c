/******************************************************************************
 * @file            nanosleep.c
 *****************************************************************************/
#include    <errno.h>
#include    <time.h>

int nanosleep (const struct timespec *req, struct timespec *rem) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x23), "D" (req), "S" (rem) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0xA2), "b" (req), "c" (rem));
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

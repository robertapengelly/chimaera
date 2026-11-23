/******************************************************************************
 * @file            clock_gettime.c
 *****************************************************************************/
#include    <errno.h>
#include    <time.h>

int clock_gettime (clockid_t clock_id, struct timespec *tp) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0xE4), "D" (clock_id), "S" (tp) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x109), "b" (clock_id), "c" (tp) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

/******************************************************************************
 * @file            gettimeofday.c
 *****************************************************************************/
#include    <sys/time.h>

#include    <errno.h>
#include    <stdint.h>

int gettimeofday (struct timeval *tv, void *tz) {

    intptr_t ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x60), "D" (tv), "S" (tz) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x4E), "b" (tv), "c" (tz) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = (int) -ret;
        return -1;
    
    }
    
    return 0;

}

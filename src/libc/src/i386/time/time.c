/******************************************************************************
 * @file            time.c
 *****************************************************************************/
#include    <errno.h>
#include    <stdint.h>
#include    <time.h>

time_t time (time_t *tloc) {

    intptr_t ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0xC9), "D" (tloc) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x0D), "b" (tloc) : "memory");
#endif
    
    if (ret < 0 && ret >= -4095) {
    
        errno = (int) -ret;
        return (time_t) -1;
    
    }
    
    return (time_t) ret;

}

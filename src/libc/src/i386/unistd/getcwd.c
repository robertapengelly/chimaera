/******************************************************************************
 * @file            getcwd.c
 *****************************************************************************/
#include    <errno.h>
#include    <stdint.h>
#include    <unistd.h>

char *getcwd (char *__buf, size_t __size) {

    intptr_t ret;
    
    if (!__buf || !__size) {
    
        errno = EINVAL;
        return 0;
    
    }
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x4F), "D" (__buf), "S" (__size) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0xB7), "b" (__buf), "c" (__size) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = (int) -ret;
        return 0;
    
    }
    
    return __buf;

}

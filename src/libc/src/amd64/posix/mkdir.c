/******************************************************************************
 * @file            mkdir.c
 *****************************************************************************/
#include    <sys/stat.h>

#include    <errno.h>
#include    <stdint.h>

int mkdir (const char *path, mode_t mode) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x53), "D" (path), "S" (mode) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x27), "b" (path), "c" (mode) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

/******************************************************************************
 * @file            rename.c
 *****************************************************************************/
#include    <errno.h>
#include    <stdio.h>

int rename (const char *old_path, const char *new_path) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x52), "D" (old_path), "S" (new_path) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x26), "b" (old_path), "c" (new_path) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

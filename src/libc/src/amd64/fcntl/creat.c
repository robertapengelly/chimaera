/******************************************************************************
 * @file            creat.c
 *****************************************************************************/
#include    <sys/types.h>

#include    <errno.h>
#include    <fcntl.h>
#include    <unistd.h>

int creat (const char *__file, mode_t __mode) {

    int ret, flags = 1 | 0100 | 01000;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x02), "D" (__file), "S" (flags), "d" (__mode) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x05), "b" (__file), "c" (flags), "d" (__mode));
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    close (ret);
    return 0;

}

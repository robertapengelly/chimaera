/******************************************************************************
 * @file            read.c
 *****************************************************************************/
#include    <sys/types.h>
#include    <errno.h>

off_t lseek (int __fd, off_t __offset, int __whence) {

    off_t ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x08), "D" (__fd), "S" (__offset), "d" (__whence) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x13), "b" (__fd), "c" (__offset), "d" (__whence));
#endif

    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

/******************************************************************************
 * @file            open.c
 *****************************************************************************/
#include    <sys/types.h>

#include    <errno.h>
#include    <fcntl.h>
#include    <stdarg.h>

int open (const char *__file, int __oflag, ...) {

    mode_t mode = 0;
    int fd, flags = 0;
    
    if (__oflag & O_CREAT) {
    
        va_list ap;
        va_start (ap, __oflag);
        mode = va_arg (ap, mode_t);
        va_end (ap);
    
    }
    
    if (__oflag & O_RDWR) {
        flags = 2;
    } else if (__oflag & O_WRONLY) {
        flags = 1;
    }
    
    if (__oflag & O_APPEND) {
        flags |= 02000;
    }
    
    if (__oflag & O_NONBLOCK) {
        flags |= 04000;
    }
    
    if (__oflag & O_CREAT) {
        flags |= 0100;
    }
    
    if (__oflag & O_TRUNC) {
        flags |= 01000;
    }
    
    if (__oflag & O_EXCL) {
        flags |= 0200;
    }
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (fd) : "a" (0x02), "D" (__file), "S" (flags), "d" (mode) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (fd) : "a" (0x05), "b" (__file), "c" (flags), "d" (mode));
#endif
    
    if (fd >= 0) {
        return fd;
    }
    
    errno = -fd;
    return -1;

}

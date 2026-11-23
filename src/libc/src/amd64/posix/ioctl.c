/******************************************************************************
 * @file            ioctl.c
 *****************************************************************************/
#include    <sys/ioctl.h>

#include    <errno.h>
#include    <stdarg.h>

int ioctl (int fd, unsigned long request, ...) {

    va_list ap;
    
    void *arg;
    int ret;
    
    va_start (ap, request);
    arg = va_arg (ap, void *);
    va_end (ap);
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x10), "D" (fd), "S" (request), "d" (arg) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x36), "b" (fd), "c" (request), "d" (arg) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

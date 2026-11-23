/******************************************************************************
 * @file            fcntl.c
 *****************************************************************************/
#include    <errno.h>
#include    <fcntl.h>
#include    <stdarg.h>

int fcntl (int __fd, int __cmd, ...) {

    int arg = 0, ret, flags;
    
    if (__cmd == F_SETFL) {
    
        va_list ap;
        
        va_start (ap, __cmd);
        flags = va_arg (ap, int);
        va_end (ap);
        
        if (flags & O_APPEND) {
            arg |= 02000;
        }
        
        if (flags & O_NONBLOCK) {
            arg |= 04000;
        }
    
    } else if (__cmd != F_GETFL) {
    
        errno = EINVAL;
        return -1;
    
    }
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x48), "D" (__fd), "S" (__cmd), "d" (arg) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x37), "b" (__fd), "c" (__cmd), "d" (arg) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    if (__cmd == F_GETFL) {
    
        flags = O_RDONLY;
        
        if ((ret & 3) == 2) {
            flags = O_RDWR;
        } else if ((ret & 3) == 1) {
            flags = O_WRONLY;
        }
        
        if (ret & 02000) {
            flags |= O_APPEND;
        }
        
        if (ret & 04000) {
            flags |= O_NONBLOCK;
        }
        
        return flags;
    
    }
    
    return ret;

}

/******************************************************************************
 * @file            write.c
 *****************************************************************************/
#include    <sys/types.h>

#include    <errno.h>
#include    <stddef.h>
#include    <unistd.h>

ssize_t write (int __fd, void *__buf, size_t __nbytes) {

    ssize_t len = 0;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (len) : "a" (0x01), "D" (__fd), "S" (__buf), "d" (__nbytes) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (len) : "a" (0x04), "b" (__fd), "c" (__buf), "d" (__nbytes));
#endif

    if (len >= 0) {
        return len;
    }
    
    errno = (int) -len;
    return -1;

}

/******************************************************************************
 * @file            read.c
 *****************************************************************************/
#include    <sys/types.h>

#include    <errno.h>
#include    <stddef.h>
#include    <unistd.h>

ssize_t read (int __fd, void *__buf, size_t __nbytes) {

    ssize_t len = 0;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (len) : "a" (0x00), "D" (__fd), "S" (__buf), "d" (__nbytes) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (len) : "a" (0x03), "b" (__fd), "c" (__buf), "d" (__nbytes));
#endif

    if (len >= 0) {
        return len;
    }
    
    errno = (int) -len;
    return -1;

}

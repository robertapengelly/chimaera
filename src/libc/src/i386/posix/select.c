/******************************************************************************
 * @file            select.c
 *****************************************************************************/
#include    <sys/select.h>

#include    <errno.h>

int select (int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("mov r10, %5\n\tmov r8, %6\n\tsyscall" : "=a" (ret) : "a" (0x17), "D" (nfds), "S" (readfds), "d" (writefds), "m" (exceptfds), "m" (timeout) : "r8", "r10", "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x8E), "b" (nfds), "c" (readfds), "d" (writefds), "S" (exceptfds), "D" (timeout) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

/******************************************************************************
 * @file            waitpid.c
 *****************************************************************************/
#include    <sys/wait.h>
#include    <errno.h>

pid_t waitpid (pid_t pid, int *status, int options) {

    pid_t ret;

#if     defined (__AMD64__)
    __asm__ __volatile__ ("xor r10, r10\n\tsyscall" : "=a" (ret) : "a" (0x3D), "D" (pid), "S" (status), "d" (options) : "r10", "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x07), "b" (pid), "c" (status), "d" (options) : "memory");
#endif

    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }

    return ret;

}

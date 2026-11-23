/******************************************************************************
 * @file            execve.c
 *****************************************************************************/
#include    <errno.h>
#include    <unistd.h>

int execve (const char *__path, char *const __argv[], char *const __envp[]) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x3B), "D" (__path), "S" (__argv), "d" (__envp) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x0B), "b" (__path), "c" (__argv), "d" (__envp) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    return ret;

}

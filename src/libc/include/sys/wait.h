/******************************************************************************
 * @file            wait.h
 *****************************************************************************/
#ifndef     _SYS_WAIT_H
#define     _SYS_WAIT_H

#include    <sys/types.h>
#define     WNOHANG                         1

#define     WEXITSTATUS(status)             (((status) >> 8) & 0xFF)
#define     WIFEXITED(status)               (((status) & 0x7F) == 0)

#define     WIFSIGNALED(status)             (((status) & 0x7F) != 0 && ((status) & 0x7F) != 0x7F)
#define     WTERMSIG(status)                ((status) & 0x7F)

pid_t waitpid (pid_t __pid, int *__status, int __options);
pid_t wait (int *__status);

#endif      /* _SYS_WAIT_H */

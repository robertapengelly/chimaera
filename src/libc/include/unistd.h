/******************************************************************************
 * @file            unistd.h
 *****************************************************************************/
#ifndef     _UNISTD_H
#define     _UNISTD_H

#include    <sys/types.h>
#include    <stddef.h>

#define     STDIN_FILENO                0
#define     STDOUT_FILENO               1
#define     STDERR_FILENO               2

#define     F_OK                        0
#define     X_OK                        1
#define     W_OK                        2
#define     R_OK                        4

ssize_t read (int __fd, void *__buf, size_t __nbytes);
ssize_t write (int __fd, void *__buf, size_t __nbytes);

char *getcwd (char *__buf, size_t __size);
int chdir (const char *__path);

int close (int __fd);
pid_t fork (void);

int unlink (const char *__path);
int rmdir (const char *__path);

off_t lseek (int __fd, off_t __offset, int __whence);
void _exit (int status) __attribute__ ((noreturn));

int access (const char *__path, int __mode);
int isatty (int __fd);

int dup2 (int __fd, int __newfd);
int dup (int __fd);

int ftruncate (int __fd, off_t __length);
int pipe (int __fds[2]);

extern char **__environ;
int execve (const char *__path, char *const __argv[], char *const __envp[]);

#endif      /* _UNISTD_H */

/******************************************************************************
 * @file            fcntl.h
 *****************************************************************************/
#ifndef     _FCNTL_H
#define     _FCNTL_H

#include    <sys/types.h>

#define     O_RDONLY                    (1U << 0)           /* open for reading only */
#define     O_WRONLY                    (1U << 1)           /* open for writing only */
#define     O_RDWR                      (1U << 2)           /* open for reading and writing */
#define     O_BINARY                    (1U << 3)           /* open as binary */
#define     O_APPEND                    (1U << 4)           /* append to the end */
#define     O_CREAT                     (1U << 5)           /* create if nonexistent */
#define     O_TRUNC                     (1U << 6)           /* truncate to zero length */
#define     O_EXCL                      (1U << 7)           /* error if already exists */
#define     O_NONBLOCK                  (1U << 11)          /* non-blocking mode */

#define     O_ACCMODE                   (O_RDONLY | O_WRONLY | O_RDWR)          /* mask for above modes */

#define     F_GETFL                     3
#define     F_SETFL                     4

int open (const char *__file, int __oflag, ...);
int creat (const char *__path, mode_t __mode);
int fcntl (int __fd, int __cmd, ...);

#endif      /* _FCNTL_H */

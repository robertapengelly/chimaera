/******************************************************************************
 * @file            select.h
 *****************************************************************************/
#ifndef     _SYS_SELECT_H
#define     _SYS_SELECT_H

#include    <sys/time.h>
#include    <sys/types.h>

#include    <stdint.h>
#include    <string.h>

#define     FD_SETSIZE                  1024

#if     defined (__AMD64__)
typedef     uint64_t                    __fd_mask;
#else
typedef     uint32_t                    __fd_mask;
#endif

#define     __NFDBITS                   (sizeof (__fd_mask) * 8)

typedef struct {
    __fd_mask fds_bits[FD_SETSIZE / __NFDBITS];
} fd_set;

#define     FD_ZERO(set)                memset ((set), 0, sizeof (fd_set))
#define     FD_SET(fd, set)             ((set)->fds_bits[(fd) / __NFDBITS] |= ((__fd_mask) 1 << ((fd) % __NFDBITS)))
#define     FD_CLR(fd, set)             ((set)->fds_bits[(fd) / __NFDBITS] &= ~((__fd_mask) 1 << ((fd) % __NFDBITS)))
#define     FD_ISSET(fd, set)           (((set)->fds_bits[(fd) / __NFDBITS] & ((__fd_mask) 1 << ((fd) % __NFDBITS))) != 0)

int select (int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

#endif      /* _SYS_SELECT_H */

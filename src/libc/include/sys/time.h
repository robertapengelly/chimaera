/******************************************************************************
 * @file            time.h
 *****************************************************************************/
#ifndef     _SYS_TIME_H
#define     _SYS_TIME_H

#include    <sys/types.h>
#include    <time.h>

#ifndef     _STRUCT_TIMEVAL
#define     _STRUCT_TIMEVAL

struct timeval {

    time_t tv_sec;
    suseconds_t tv_usec;

};

#endif

int gettimeofday (struct timeval *tv, void *tz);

#endif      /* _SYS_TIME_H */

/******************************************************************************
 * @file            time.h
 *****************************************************************************/
#ifndef     _TIME_H
#define     _TIME_H

#define     __NEED_SIZE_T
#include    <stddef.h>

#if     defined (__AMD64__)
typedef     signed long long            time_t;
#else
typedef     signed long                 time_t;
#endif

typedef     signed int                  clockid_t;

#define     CLOCK_REALTIME              0
#define     CLOCK_MONOTONIC             1

struct timespec {

    time_t tv_sec;
    long tv_nsec;

};

time_t time (time_t *tloc);

int clock_gettime (clockid_t clock_id, struct timespec *tp);
int nanosleep (const struct timespec *req, struct timespec *rem);

struct tm {

    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;

};

size_t strftime (char *s, size_t maxsize, const char *format, const struct tm *timeptr);
time_t mktime (struct tm *timeptr);

struct tm *localtime (const time_t *timer);

#endif      /* _TIME_H */

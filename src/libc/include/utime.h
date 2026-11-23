/******************************************************************************
 * @file            utime.h
 *****************************************************************************/
#ifndef     _UTIME_H
#define     _UTIME_H

#include    <time.h>

struct utimbuf {

    time_t actime;
    time_t modtime;

};

int utime (const char *__path, const struct utimbuf *__times);

#endif      /* _UTIME_H */

/******************************************************************************
 * @file            types.h
 *****************************************************************************/
#ifndef     _SYS_TYPES_H
#define     _SYS_TYPES_H

#if     defined (__AMD64__)
typedef     unsigned long long          ino_t;
typedef     signed long long            ssize_t;
#else
typedef     unsigned int                ino_t;
typedef     signed int                  ssize_t;
#endif

typedef     unsigned int                mode_t;
typedef     signed int                  pid_t;

typedef     signed int                  off_t;

#if     defined (__AMD64__)
typedef     signed long long            suseconds_t;
#else
typedef     signed long                 suseconds_t;
#endif

typedef     unsigned long long          fsblkcnt_t;
typedef     unsigned long long          fsfilcnt_t;

#endif      /* _SYS_TYPES_H */

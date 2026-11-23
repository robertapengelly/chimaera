/******************************************************************************
 * @file            stdint.h
 *****************************************************************************/
#ifndef     _STDINT_H
#define     _STDINT_H

#define     __NEED_WCHAR_T
#include    <stddef.h>

typedef     signed char                 int8_t;
typedef     unsigned char               uint8_t;

typedef     signed short                int16_t;
typedef     unsigned short              uint16_t;

typedef     signed int                  int32_t;
typedef     unsigned int                uint32_t;

typedef     signed long long            int64_t;
typedef     unsigned long long          uint64_t;

#if     defined (__AMD64__)
typedef     signed long long            intptr_t;
typedef     unsigned long long          uintptr_t;
#else
typedef     signed int                  intptr_t;
typedef     unsigned int                uintptr_t;
#endif

typedef     signed long long            intmax_t;
typedef     unsigned long long          uintmax_t;

#define     UINT64_C(_c)                (_c ## ULL)
#define     UINT64_MAX                  UINT64_C (18446744073709551615)

#endif      /* _STDINT_H */

/******************************************************************************
 * @file            fnv1a.h
 *****************************************************************************/
#ifndef     _FNV1A_H
#define     _FNV1A_H

#include    <stddef.h>
#include    <stdint.h>

uint64_t checksum64_fnv1a (const void *data, size_t len);

#endif      /* _FNV1A_H */

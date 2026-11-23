/******************************************************************************
 * @file            tables.h
 *****************************************************************************/
#ifndef     _TABLES_H
#define     _TABLES_H

#include    <stdint.h>

/* Element x contains the value of x with the bits in reverse order. */
extern const uint8_t reverse8_tbl[UINT8_MAX + 1];

/* Code lengths for fixed Huffman coding of litlen and dist symbols. */
extern unsigned char fixed_litlen_lengths[288];
extern unsigned char fixed_dist_lengths[32];

/**
 * Table of litlen symbol values miuns 257 with corresponding base
 * length and number of extra bits.
 */
struct litlen_tbl_t { uint16_t base_len : 9, ebits : 7; };
extern struct litlen_tbl_t litlen_tbl[29];

/**
 * Table of dist symbol values with corresponding base distance and
 * number of extra bits.
 */
struct dist_tbl_t { uint16_t base_dist, ebits; };
extern struct dist_tbl_t dist_tbl[30];

/* Table for computing CRC-32. */
extern const uint32_t crc32_tbl[256];

#endif      /* _TABLES_H */

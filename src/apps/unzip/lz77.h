/******************************************************************************
 * @file            lz77.h
 *****************************************************************************/
#ifndef     _LZ77_H
#define     _LZ77_H

#include    <stdint.h>
#include    <stdio.h>

int lz77_output_lit (FILE *fp, uint64_t dst_pos, unsigned char lit);
int lz77_output_backref (FILE *fp, uint64_t dst_pos, uint64_t dist, uint64_t len);
int lz77_output_backref64 (FILE *fp, uint64_t dst_pos, uint64_t dist, uint64_t len);

#endif      /* _LZ77_H */

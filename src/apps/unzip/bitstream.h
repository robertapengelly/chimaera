/******************************************************************************
 * @file            bitstream.h
 *****************************************************************************/
#ifndef     _BITSTREAM_H
#define     _BITSTREAM_H

#include    <stdint.h>

/* Input bitstream. */
typedef struct {

    unsigned char *src;                 /* Source bytes. */
    unsigned char *end;                 /* Past-the-end byte of src*/
    
    uint64_t bitpos;                    /* Position of the next bit to read. */
    uint64_t bitpos_end;                /* Position of past-the-end bit. */

} istream_t;

#define         ISTREAM_MIN_BITS        (64 - 7)
unsigned char *istream_byte_align (istream_t *is);

int istream_init (istream_t *is, unsigned char *s, uint64_t n);
int istream_advance (istream_t *is, uint64_t n);

uint64_t istream_bits (istream_t *is);
uint64_t istream_bytes_read (istream_t *is);

#endif      /* _BITSTREAM_H */

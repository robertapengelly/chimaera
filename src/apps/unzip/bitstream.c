/******************************************************************************
 * @file            bitstream.c
 *****************************************************************************/
#include    <assert.h>
#include    <limits.h>
#include    <stdint.h>
#include    <stdlib.h>
#include    <string.h>

#include    "bitstream.h"
#include    "lib.h"

int istream_init (istream_t *is, unsigned char *s, uint64_t n) {

    is->end = (is->src = s) + n;
    
    is->bitpos_end = n * 8;
    is->bitpos = 0;
    
    return 0;

}

int istream_advance (istream_t *is, uint64_t n) {

    assert (is->bitpos <= is->bitpos_end);
    
    if (is->bitpos_end - is->bitpos < n) {
        return 0;
    }
    
    is->bitpos += n;
    return 1;

}

unsigned char *istream_byte_align (istream_t *is) {

    unsigned char *byte;
    
    assert (is->bitpos <= is->bitpos_end && "not past end of stream");
    is->bitpos = round_up (is->bitpos, 8);
    
    byte = is->src + (is->bitpos / 8);
    assert (byte <= is->end);
    
    return byte;

}

uint64_t istream_bits (istream_t *is) {

    unsigned char *next;
    
    uint64_t bits, i;
    int cnt = 8;
    
    assert ((next = is->src + (is->bitpos / 8)) <= is->end && "cannot read past end of stream");
    
    if (is->end - next >= cnt) {
    
        /* Common case: read 4 bytes in one go. */
        bits = array_to_integer (next, cnt, 0);
    
    } else {
    
        /* Read the available bits and zero-pad. */
        bits = 0;
        
        for (i = 0; i < (uint64_t) (is->end - next); i++) {
            bits |= (uint64_t) next[i] << (i * CHAR_BIT);
        }
    
    }
    
    return bits >> (is->bitpos % 8);

}

uint64_t istream_bytes_read (istream_t *is) {
    return round_up (is->bitpos, 8) / 8;
}

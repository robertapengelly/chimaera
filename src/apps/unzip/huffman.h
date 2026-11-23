/******************************************************************************
 * @file            huffman.h
 *****************************************************************************/
#ifndef     _HUFFMAN_H
#define     _HUFFMAN_H

#include    <stdint.h>

#define     MAX_HUFFMAN_SYMBOLS                             288                 /* Deflate uses max 288 symbols. */
#define     MAX_HUFFMAN_BITS                                16                  /* Implode uses max 16-bit codewords. */
#define     HUFFMAN_LOOKUP_TABLE_BITS                       8                   /* Seems a good trade-off. */

typedef struct {

    uint64_t num_syms;
    
    /* Lookup table for fast deocding of short codewords. */
    struct {
    
        uint16_t sym : 9;               /* Wide enough to fit the max symbol mbr. */
        uint16_t len : 7;               /* 0 means no symbol. */
    
    } table[1U << HUFFMAN_LOOKUP_TABLE_BITS];
    
    /* "Sentinel bits" value for each codeword length. */
    uint32_t sentinel_bits[MAX_HUFFMAN_BITS + 1];
    
    /* First symbol index minus first codeword mod 2**16 for each length. */
    uint16_t offset_first_sym_idx[MAX_HUFFMAN_BITS + 1];
    
    /* Map from symbol index to symbol. */
    uint16_t syms[MAX_HUFFMAN_SYMBOLS];

} huffman_decoder_t;

/**
 * Initialize huffman decoder d for a code defined by the n codeword lengths.
 * Returns false if the codeword lengths do not correspond to a valid prefix code.
 */
int huffman_decoder_init (huffman_decoder_t *d, unsigned char *lengths, uint64_t n);

/**
 * Use the decoder d to decode a symbol from the LSB-first zero-padded bits.
 * Returns the decoded symbol number or -1 if no symbol could be decoded.
 * *num_used_bits will be set to the number of bits used to decode the symbol, or zero if no symbol could be decoded.
 */
int huffman_decode (const huffman_decoder_t *d, uint16_t bits, uint64_t *num_used_bits);

#endif      /* _HUFFMAN_H */

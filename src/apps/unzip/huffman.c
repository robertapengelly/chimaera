/******************************************************************************
 * @file            huffman.c
 *****************************************************************************/
#include    <assert.h>
#include    <limits.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>

#include    "huffman.h"
#include    "lib.h"
#include    "tables.h"

static uint16_t reverse16 (uint16_t x, int n) {

    uint16_t reversed, lo, hi;
    
    assert (n > 0);
    assert (n <= 16);
    
    lo = x & UCHAR_MAX;
    hi = x >> CHAR_BIT;
    
    reversed = (uint16_t) ((reverse8_tbl[lo] << CHAR_BIT) | reverse8_tbl[hi]);
    return reversed >> (16 - n);

}

static void table_insert (huffman_decoder_t *d, uint64_t sym, uint64_t len, uint16_t codeword) {

    uint16_t padding, index;
    int pad_len;
    
    assert (len <= HUFFMAN_LOOKUP_TABLE_BITS);
    
    codeword = reverse16 (codeword, len);
    pad_len = HUFFMAN_LOOKUP_TABLE_BITS - len;
    
    /* Pad the pad_len upper bits with all bit combinations. */
    for (padding = 0; padding < (1U << pad_len); padding++) {
    
        index = (uint16_t) (codeword | (padding << len));
        
        d->table[index].sym = (uint16_t) sym;
        d->table[index].len = (uint16_t) len;
        
        assert (d->table[index].sym == sym && "fits in bitfield");
        assert (d->table[index].len == len && "fits in bitfield");
    
    }

}

int huffman_decoder_init (huffman_decoder_t *d, unsigned char *lengths, uint64_t n) {

    uint16_t count[MAX_HUFFMAN_BITS + 1] = { 0 };
    uint16_t code[MAX_HUFFMAN_BITS + 1] = { 0 };
    
    uint16_t sym_idx[MAX_HUFFMAN_BITS + 1] = { 0 };
    
    uint64_t i, l;
    uint32_t s;
    
    assert (n <= MAX_HUFFMAN_SYMBOLS);
    d->num_syms = n;
    
    /* Zero-initialize the lookup table. */
    for (i = 0; i < sizeof (d->table) / sizeof (d->table[0]); i++) {
        d->table[i].len = 0;
    }
    
    /* Count the number of codewords of each length. */
    for (i = 0; i < n; i++) {
    
        assert (lengths[i] <= MAX_HUFFMAN_BITS);
        count[lengths[i]]++;
    
    }
    
    count[0] = 0;                       /* Ignore zero-length codeword. */
    
    /* Compute sentinel bits and offset first sym_idx for each length. */
    code[0] = 0;
    sym_idx[0] = 0;
    
    for (l = 1; l <= MAX_HUFFMAN_BITS; l++) {
    
        /* First canonical codeword of this length. */
        code[l] = (uint16_t) ((code[l - 1] + count[l - 1]) << 1);
        
        if (count[l] != 0 && code[l] + count[l] - 1 > (1 << l) - 1) {
        
            /* The last codeword is longer than l bits. */
            return 0;
        
        }
        
        s = (uint32_t) (((uint32_t) code[l] + (uint32_t) count[l]) << (MAX_HUFFMAN_BITS - l));
        
        d->sentinel_bits[l] = s;
        assert (d->sentinel_bits[l] >= code[l] && "no overflow!");
        
        sym_idx[l] = sym_idx[l - 1] + count[l - 1];
        d->offset_first_sym_idx[l] = sym_idx[l] - code[l];
    
    }
    
    /* Build mapping from index to symbol and populate the loopup table. */
    for (i = 0; i < n; i++) {
    
        if ((l = lengths[i]) == 0) {
            continue;
        }
        
        d->syms[sym_idx[l]] = (uint16_t) i;
        sym_idx[l]++;
        
        if (l <= HUFFMAN_LOOKUP_TABLE_BITS) {
        
            table_insert (d, i, l, code[l]);
            code[l]++;
        
        }
    
    }
    
    return 1;

}

int huffman_decode (const huffman_decoder_t *d, uint16_t bits, uint64_t *num_used_bits) {

    uint64_t lookup_bits, sym_idx, l;
    
    /* First try the lookup table. */
    lookup_bits = lsb (bits, HUFFMAN_LOOKUP_TABLE_BITS);
    assert (lookup_bits < sizeof (d->table) / sizeof (d->table[0]));
    
    if (d->table[lookup_bits].len != 0) {
    
        assert (d->table[lookup_bits].len <= HUFFMAN_LOOKUP_TABLE_BITS);
        assert (d->table[lookup_bits].sym < d->num_syms);
        
        *num_used_bits = d->table[lookup_bits].len;
        return d->table[lookup_bits].sym;
    
    }
    
    /* Then do canonical decoding with the bits in MSB-first order. */
    bits = reverse16 (bits, MAX_HUFFMAN_BITS);
    
    for (l = HUFFMAN_LOOKUP_TABLE_BITS + 1; l <= MAX_HUFFMAN_BITS; l++) {
    
        if (bits < d->sentinel_bits[l]) {
        
            bits >>= MAX_HUFFMAN_BITS - l;
            
            sym_idx = (uint16_t) (d->offset_first_sym_idx[l] + bits);
            assert (sym_idx < d->num_syms);
            
            *num_used_bits = l;
            return d->syms[sym_idx];
        
        }
    
    }
    
    *num_used_bits = 0;
    return -1;

}

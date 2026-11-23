/******************************************************************************
 * @file            inflate.c
 *****************************************************************************/
#include    <assert.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>

#include    "bitstream.h"
#include    "huffman.h"
#include    "lib.h"
#include    "lz77.h"
#include    "tables.h"
#include    "unzip.h"

#define     LITLEN_TBL_OFFSET           257

#define     LITLEN_MAX                  285
#define     LITLEN_EOB                  256

#define     MAX_LEN                     258
#define     MIN_LEN                     3

#define     DISTSYM_MAX                 29

#define     MAX_DISTANCE                32768
#define     MIN_DISTANCE                1

#define     MAX_CODELEN_LENS            19
#define     MIN_CODELEN_LENS            4

#define     MAX_DIST_LENS               32
#define     MIN_DIST_LENS               1

#define     MAX_LITLEN_LENS             288
#define     MIN_LITLEN_LENS             257

#define     CODELEN_MAX_LIT             15

#define     CODELEN_COPY                16
#define     CODELEN_COPY_MAX            6
#define     CODELEN_COPY_MIN            3

#define     CODELEN_ZEROS               17
#define     CODELEN_ZEROS_MAX           10
#define     CODELEN_ZEROS_MIN           3

#define     CODELEN_ZEROS2_MAX          138
#define     CODELEN_ZEROS2              18
#define     CODELEN_ZEROS2_MIN          11

/* RFC 1951, 3.2.7 */
static const int codelen_lengths_order[MAX_CODELEN_LENS] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

static inf_stat_t inf_block (istream_t *is, FILE *outfile, uint64_t dst_cap, uint64_t *dst_pos, huffman_decoder_t *litlen_dec, huffman_decoder_t *dist_dec) {

    uint64_t bits, used, dist, len;
    uint16_t ebits;
    
    int litlen, distsym;
    uint64_t used_tot;
    
    for (;;) {
    
        /* Read a litlen symbol. */
        bits = istream_bits (is);
        
        litlen = huffman_decode (litlen_dec, (uint16_t) bits, &used);
        /*printf ("litlen: %d\n", litlen);*/
        
        bits >>= used;
        used_tot = used;
        
        if (litlen < 0 || litlen > LITLEN_MAX) {
        
            /* Failed to decode, or invalid symbol. */
            return HWINF_ERR;
        
        } else if (litlen <= (int) UINT8_MAX) {
        
            if (!istream_advance (is, used_tot)) {
                return HWINF_ERR;
            }
            
            if (*dst_pos == dst_cap) {
                return HWINF_FULL;
            }
            
            if (lz77_output_lit (outfile, (*dst_pos)++, (uint8_t) litlen)) {
                return HWINF_ERR;
            }
            
            continue;
        
        } else if (litlen == LITLEN_EOB) {
        
            /* End of block. */
            if (!istream_advance (is, used_tot)) {
                return HWINF_ERR;
            }
            
            return HWINF_OK;
        
        }
        
        assert (litlen >= LITLEN_TBL_OFFSET && litlen <= LITLEN_MAX);
        
        /* It is a back reference.  Figure out the length.*/
        len = litlen_tbl[litlen - LITLEN_TBL_OFFSET].base_len;
        
        if ((ebits = litlen_tbl[litlen - LITLEN_TBL_OFFSET].ebits) != 0) {
        
            len += lsb (bits, ebits);
            
            bits >>= ebits;
            used_tot += ebits;
        
        }
        
        assert (len >= MIN_LEN && len <= MAX_LEN);
        distsym = huffman_decode (dist_dec, (uint16_t) bits, &used);
        
        bits >>= used;
        used_tot += used;
        
        if (distsym < 0 || distsym > DISTSYM_MAX) {
            return HWINF_ERR;
        }
        
        dist = dist_tbl[distsym].base_dist;
        
        if ((ebits = dist_tbl[distsym].ebits) != 0) {
        
            dist += lsb (bits, ebits);
            bits >>= ebits;
            used_tot += ebits;
        
        }
        
        assert (dist >= MIN_DISTANCE && dist <= MAX_DISTANCE);
        assert (used_tot <= ISTREAM_MIN_BITS);
        
        if (!istream_advance (is, used_tot)) {
            return HWINF_ERR;
        }
        
        /* Bounds check and output the backref. */
        if (dist > *dst_pos) {
            return HWINF_ERR;
        }
        
        if (round_up (len, 8) <= dst_cap - *dst_pos) {
        
            if (lz77_output_backref64 (outfile, *dst_pos, dist, len)) {
                return HWINF_ERR;
            }
        
        } else if (len <= dst_cap - *dst_pos) {
        
            if (lz77_output_backref (outfile, *dst_pos, dist, len)) {
                return HWINF_ERR;
            }
        
        } else {
            return HWINF_FULL;
        }
        
        (*dst_pos) += len;
    
    }

}

static inf_stat_t init_dyn_decoders (istream_t *is, huffman_decoder_t *litlen_dec, huffman_decoder_t *dist_dec) {

    uint64_t num_litlen_lens, num_dist_lens, num_codelen_lens;
    uint64_t i, n, used;
    
    static unsigned char code_lengths[MAX_LITLEN_LENS + MAX_DIST_LENS] = { 0 };
    static unsigned char codelen_lengths[MAX_CODELEN_LENS] = { 0 };
    
    huffman_decoder_t codelen_dec;
    int sym;
    
    uint64_t bits = istream_bits (is);
    
    /* Number of litlen codeword lengths (5 bits + 257). */
    num_litlen_lens = (uint64_t) lsb (bits, 5) + MIN_LITLEN_LENS;
    bits >>= 5;
    
    assert (num_litlen_lens <= MAX_LITLEN_LENS);
    
    /* Number of codeword lengths (5 bits + 1). */
    num_dist_lens = (uint64_t) lsb (bits, 5) + MIN_DIST_LENS;
    bits >>= 5;
    
    assert (num_dist_lens <= MAX_DIST_LENS);
    
    /* Number of code length lengths (4 bits + 4). */
    num_codelen_lens = (uint64_t) lsb (bits, 4) + MIN_CODELEN_LENS;
    bits >>= 4;
    
    assert (num_codelen_lens <= MAX_CODELEN_LENS);
    
    if (!istream_advance (is, 5 + 5 + 4)) {
        return HWINF_ERR;
    }
    
    /**
     * Read the codelen codeword lengths (3 bits each)
     * and initialize the codelen decoder.
     */
    for (i = 0; i < num_codelen_lens; i++) {
    
        bits = istream_bits (is);
        codelen_lengths[codelen_lengths_order[i]] = (unsigned char) lsb (bits, 3);
        
        if (!istream_advance (is, 3)) {
            return HWINF_ERR;
        }
    
    }
    
    for (; i < MAX_CODELEN_LENS; i++) {
        codelen_lengths[codelen_lengths_order[i]] = 0;
    }
    
    if (!huffman_decoder_init (&codelen_dec, codelen_lengths, MAX_CODELEN_LENS)) {
        return HWINF_ERR;
    }
    
    /* Read the litlen and dist codeword lengths. */
    i = 0;
    
    while (i < num_litlen_lens + num_dist_lens) {
    
        bits = istream_bits (is);
        
        sym = huffman_decode (&codelen_dec, (uint16_t) bits, &used);
        bits >>= used;
        
        if (!istream_advance (is, used)) {
            return HWINF_ERR;
        }
        
        if (sym >= 0 && sym <= CODELEN_MAX_LIT) {
        
            /* A literal codeword length. */
            code_lengths[i++] = (unsigned char) sym;
        
        } else if (sym == CODELEN_COPY) {
        
            /* Copy the previous codeword length 3--6 times. */
            if (i < 1) {
                return HWINF_ERR;       /* No previous length. */
            }
            
            /* 2 bits + 3 */
            n = (uint64_t) lsb (bits, 2) + CODELEN_COPY_MIN;
            
            if (!istream_advance (is, 2)) {
                return HWINF_ERR;
            }
            
            assert (n >= CODELEN_COPY_MIN && n <= CODELEN_COPY_MAX);
            
            if (i + n > num_litlen_lens + num_dist_lens) {
                return HWINF_ERR;
            }
            
            while (n--) {
            
                code_lengths[i] = code_lengths[i - 1];
                i++;
            
            }
        
        } else if (sym == CODELEN_ZEROS) {
        
            /* 3--10 zeros; 3 bits + 3 */
            n = (uint64_t) lsb (bits, 3) + CODELEN_ZEROS_MIN;
            
            if (!istream_advance (is, 3)) {
                return HWINF_ERR;
            }
            
            assert (n >= CODELEN_ZEROS_MIN && n <= CODELEN_ZEROS_MAX);
            
            if (i + n > num_litlen_lens + num_dist_lens) {
                return HWINF_ERR;
            }
            
            while (n--) { code_lengths[i++] = 0; }
        
        } else if (sym == CODELEN_ZEROS2) {
        
            /* 11--138 zeros; 7 bits + 11 */
            n = (uint64_t) lsb (bits, 7) + CODELEN_ZEROS2_MIN;
            
            if (!istream_advance (is, 7)) {
                return HWINF_ERR;
            }
            
            assert (n >= CODELEN_ZEROS2_MIN && n <= CODELEN_ZEROS2_MAX);
            
            if (i + n > num_litlen_lens + num_dist_lens) {
                return HWINF_ERR;
            }
            
            while (n--) { code_lengths[i++] = 0; }
        
        } else {
        
            /* Invalid symbol. */
            return HWINF_ERR;
        
        }
    
    }
    
    if (!huffman_decoder_init (litlen_dec, &code_lengths[0], num_litlen_lens)) {
        return HWINF_ERR;
    }
    
    if (!huffman_decoder_init (dist_dec, &code_lengths[num_litlen_lens], num_dist_lens)) {
        return HWINF_ERR;
    }
    
    return HWINF_OK;

}

static inf_stat_t inf_dyn_block (istream_t *is, FILE *outfile, uint64_t dst_cap, uint64_t *dst_pos) {

    static huffman_decoder_t litlen_dec = { 0 }, dist_dec = { 0 };
    inf_stat_t stat;
    
    if ((stat = init_dyn_decoders (is, &litlen_dec, &dist_dec)) != HWINF_OK) {
        return stat;
    }
    
    return inf_block (is, outfile, dst_cap, dst_pos, &litlen_dec, &dist_dec);

}

static inf_stat_t inf_fixed_block (istream_t *is, FILE *outfile, uint64_t dst_cap, uint64_t *dst_pos) {

    static huffman_decoder_t litlen_dec = { 0 }, dist_dec = { 0 };
    
    huffman_decoder_init (&litlen_dec, fixed_litlen_lengths, sizeof (fixed_litlen_lengths) / sizeof (fixed_litlen_lengths[0]));
    huffman_decoder_init (&dist_dec, fixed_dist_lengths, sizeof (fixed_dist_lengths) / sizeof (fixed_dist_lengths[0]));
    
    return inf_block (is, outfile, dst_cap, dst_pos, &litlen_dec, &dist_dec);

}

static inf_stat_t inf_noncomp_block (istream_t *is, FILE *outfile, uint64_t dst_cap, uint64_t *dst_pos) {

    unsigned char *p;
    uint16_t len, nlen;
    
    p = istream_byte_align (is);
    
    /* Read len and nlen (2 x 16 bits). */
    if (!istream_advance (is, 32)) {
        return HWINF_ERR;               /* Not enough input. */
    }
    
    len = array_to_integer (p, 2, 0), p += 2;
    nlen = array_to_integer (p, 2, 0), p += 2;
    
    if (nlen != (~len & 0xffff)) {
        return HWINF_ERR;
    }
    
    if (!istream_advance (is, len * 8)) {
        return HWINF_ERR;               /* Not enough input. */
    }
    
    if (dst_cap - *dst_pos < len) {
        return HWINF_ERR;               /* Not enough room to output. */
    }
    
    if (fseek (outfile, *dst_pos, SEEK_SET)) {
        return HWINF_ERR;
    }
    
    if (fwrite (p, 1, len, outfile) != len) {
        return HWINF_ERR;               /* Something went wrong. */
    }
    
    *dst_pos += len;
    return HWINF_OK;

}

inf_stat_t hwinflate (unsigned char *src, uint64_t src_len, uint64_t *src_used, FILE *outfile, uint64_t dst_cap, uint64_t *dst_used) {

    inf_stat_t stat;
    istream_t is;
    
    uint64_t dst_pos, bits;
    int bfinal;
    
    if (istream_init (&is, src, src_len)) {
        return HWINF_ERR;
    }
    
    dst_pos = 0;
    
    do {
    
        bits = istream_bits (&is);
        
        if (!istream_advance (&is, 3)) {
            return HWINF_ERR;
        }
        
        bfinal = bits & 1;
        bits >>= 1;
        
        switch (lsb (bits, 2)) {
        
            case 0:                     /* No compression. */
            
                stat = inf_noncomp_block (&is, outfile, dst_cap, &dst_pos);
                break;
            
            case 1:                     /* Compressed with fixed Huffman codes. */
            
                stat = inf_fixed_block (&is, outfile, dst_cap, &dst_pos);
                break;
            
            case 2:                     /* Compressed with "dynamic Huffman codes. */
            
                stat = inf_dyn_block (&is, outfile, dst_cap, &dst_pos);
                break;
            
            default:
            
                return HWINF_ERR;
            
        }
        
        if (stat != HWINF_OK) {
            return stat;
        }
    
    } while (!bfinal);
    
    *src_used = istream_bytes_read (&is);
    
    assert (dst_pos <= dst_cap);
    *dst_used = dst_pos;
    
    return HWINF_OK;

}

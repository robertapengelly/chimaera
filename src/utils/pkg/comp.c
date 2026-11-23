/******************************************************************************
 * @file            comp.c
 *****************************************************************************/
#include    <stdint.h>
#include    <stdlib.h>
#include    <string.h>

#include    "pkg.h"
#include    "lib.h"

#define     CHC_TAG_LITERAL             0x00
#define     CHC_TAG_RLE                 0x01
#define     CHC_TAG_MATCH               0x02
#define     CHC_TAG_END                 0xff

#define     CHC_WINDOW                  4096
#define     CHC_MIN_MATCH               4
#define     CHC_MAX_MATCH               255
#define     CHC_MAX_LITERAL             255
#define     CHC_MAX_RLE                 255

struct outbuf {

    unsigned char *buf;
    uint64_t len, cap;

};

static int out_reserve (struct outbuf *o, uint64_t add) {

    uint64_t new_cap;
    
    if (o->len + add <= o->cap) {
        return 0;
    }
    
    new_cap = o->cap ? o->cap * 2 : 1024;
    
    while (o->len + add > new_cap) {
        new_cap *= 2;
    }
    
    o->buf = xrealloc (o->buf, new_cap);
    o->cap = new_cap;
    
    return 0;

}

static int out_byte (struct outbuf *o, unsigned char ch) {

    if (out_reserve (o, 1) != 0) {
        return -1;
    }
    
    o->buf[o->len++] = ch;
    return 0;

}

static int out_data (struct outbuf *o, const unsigned char *p, uint64_t n) {

    if (out_reserve (o, n) != 0) {
        return -1;
    }
    
    memcpy (o->buf + o->len, p, n);
    o->len += n;
    
    return 0;

}

static int emit_literal (struct outbuf *o, const unsigned char *p, uint64_t n) {

    while (n) {
    
        uint64_t chunk = n > CHC_MAX_LITERAL ? CHC_MAX_LITERAL : n;
        
        if (out_byte (o, CHC_TAG_LITERAL) != 0) {
            return -1;
        }
        
        if (out_byte (o, (unsigned char) chunk) != 0) {
            return -1;
        }
        
        if (out_data (o, p, chunk) != 0) {
            return -1;
        }
        
        p += chunk;
        n -= chunk;
    
    }
    
    return 0;

}

static int emit_rle (struct outbuf *o, unsigned char ch, uint64_t n) {

    if (out_byte (o, CHC_TAG_RLE) != 0) {
        return -1;
    }
    
    if (out_byte (o, (unsigned char) n) != 0) {
        return -1;
    }
    
    if (out_byte (o, ch) != 0) {
        return -1;
    }
    
    return 0;

}

static int emit_match (struct outbuf *o, uint64_t distance, uint64_t n) {

    if (out_byte (o, CHC_TAG_MATCH) != 0) {
        return -1;
    }
    
    if (out_byte (o, (unsigned char) (distance & 0xff)) != 0) {
        return -1;
    }
    
    if (out_byte (o, (unsigned char) ((distance >> 8) & 0xff)) != 0) {
        return -1;
    }
    
    if (out_byte (o, (unsigned char) n) != 0) {
        return -1;
    }
    
    return 0;

}

static uint64_t rle_length (const unsigned char *in, uint64_t size, uint64_t pos) {

    uint64_t n = 1;
    
    while (pos + n < size && n < CHC_MAX_RLE && in[pos + n] == in[pos]) {
        n++;
    }
    
    return n;

}

static void find_match (const unsigned char *in, uint64_t size, uint64_t pos, uint64_t *best_dist, uint64_t *best_len) {

    uint64_t start;
    uint64_t p;
    uint64_t max_len;
    
    *best_dist = 0;
    *best_len = 0;
    
    if (pos == 0) {
        return;
    }
    
    start = pos > CHC_WINDOW ? pos - CHC_WINDOW : 0;
    max_len = size - pos;
    
    if (max_len > CHC_MAX_MATCH) {
        max_len = CHC_MAX_MATCH;
    }
    
    for (p = start; p < pos; p++) {
    
        uint64_t n = 0;
        
        while (n < max_len && in[p + n] == in[pos + n]) {
            n++;
        }
        
        if (n >= CHC_MIN_MATCH && n > *best_len) {
        
            *best_len = n;
            *best_dist = pos - p;
            
            if (n == max_len) {
                break;
            }
        
        }
    
    }

}

int pkg_chc_compress (const unsigned char *in, uint64_t in_size, unsigned char **out, uint64_t *out_size) {

    struct outbuf o;
    
    uint64_t pos = 0;
    uint64_t lit_start = 0;
    uint64_t lit_len = 0;
    
    memset (&o, 0, sizeof (o));
    
    while (pos < in_size) {
    
        uint64_t run;
        uint64_t dist;
        uint64_t match;
        
        run = rle_length (in, in_size, pos);
        find_match (in, in_size, pos, &dist, &match);
        
        if (run >= CHC_MIN_MATCH && run >= match) {
        
            if (lit_len && emit_literal (&o, in + lit_start, lit_len) != 0) {
                return -1;
            }
            
            lit_len = 0;
            
            if (emit_rle (&o, in[pos], run) != 0) {
                return -1;
            }
            
            pos += run;
            lit_start = pos;
        
        } else if (match >= CHC_MIN_MATCH) {
        
            if (lit_len && emit_literal (&o, in + lit_start, lit_len) != 0) {
                return -1;
            }
            
            lit_len = 0;
            
            if (emit_match (&o, dist, match) != 0) {
                return -1;
            }
            
            pos += match;
            lit_start = pos;
        
        } else {
        
            if (lit_len == 0) {
                lit_start = pos;
            }
            
            lit_len++;
            pos++;
            
            if (lit_len == CHC_MAX_LITERAL) {
            
                if (emit_literal (&o, in + lit_start, lit_len) != 0) {
                    return -1;
                }
                
                lit_len = 0;
                lit_start = pos;
            
            }
        
        }
    
    }
    
    if (lit_len && emit_literal (&o, in + lit_start, lit_len) != 0) {
        return -1;
    }
    
    if (out_byte (&o, CHC_TAG_END) != 0) {
        return -1;
    }
    
    *out_size = o.len;
    *out = o.buf;
    
    return 0;

}

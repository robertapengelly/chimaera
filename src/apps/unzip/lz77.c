/******************************************************************************
 * @file            lz77.c
 *****************************************************************************/
#include    <assert.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>

#include    "lz77.h"

int lz77_output_lit (FILE *fp, uint64_t dst_pos, unsigned char lit) {

    if (fseek (fp, dst_pos, SEEK_SET)) {
        return -1;
    }
    
    if (fwrite (&lit, 1, 1, fp) != 1) {
        return -1;
    }
    
    return 0;

}

int lz77_output_backref (FILE *fp, uint64_t dst_pos, uint64_t dist, uint64_t len) {

    char byte;
    uint64_t i;
    
    assert (dist <= dst_pos && "cannot reference before beginning of dst");
    
    for (i = 0; i < len; i++) {
    
        if (fseek (fp, dst_pos - dist, SEEK_SET)) {
            return -1;
        }
        
        if (fread (&byte, 1, 1, fp) != 1) {
            return -1;
        }
        
        if (fseek (fp, dst_pos, SEEK_SET)) {
            return -1;
        }
        
        if (fwrite (&byte, 1, 1, fp) != 1) {
            return -1;
        }
        
        dst_pos++;
    
    }
    
    return 0;

}

int lz77_output_backref64 (FILE *fp, uint64_t dst_pos, uint64_t dist, uint64_t len) {

    uint64_t temp = 0, read, inc, i;
    
    assert (len > 0);
    assert (dist <= dst_pos && "cannot reference before beginning of dst");
    
    if (len > dist) {
    
        /* Self-overlapping backref, fall back to byte-by-byte copy. */
        return lz77_output_backref (fp, dst_pos, dist, len);
    
    }
    
    inc = 8;
    i = 0;
    
    do {
    
        if (fseek (fp, dst_pos - dist + i, SEEK_SET)) {
            return -1;
        }
        
        if (!(read = fread (&temp, 1, inc, fp))) {
            return -1;
        }
        
        if (fseek (fp, dst_pos + i, SEEK_SET)) {
            return -1;
        }
        
        if (fwrite (&temp, 1, read, fp) != read) {
            return -1;
        }
        
        i += inc;
    
    } while (i < len);
    
    return 0;

}

/******************************************************************************
 * @file            reader.c
 *****************************************************************************/
#include    <stdint.h>
#include    <stdlib.h>
#include    <stdio.h>
#include    <string.h>

#include    "regf.h"
#include    "lib.h"
#include    "report.h"

unsigned char *get_root_nk (unsigned char *file, size_t file_size) {

    uint32_t root_rel;
    
    unsigned char *cell;
    unsigned char *nk;
    
    if (file_size < BASE_BLOCK_SIZE) {
        return 0;
    }
    
    if (memcmp (file, "regf", 4) != 0) {
        return 0;
    }
    
    root_rel = rd32 (file + 36);
    
    if (!(cell = hive_rel_ptr (file, file_size, root_rel))) {
        return 0;
    }
    
    nk = cell + 4;
    
    if (memcmp (nk, "nk", 2) != 0) {
        return 0;
    }
    
    return nk;

}

unsigned char *hive_rel_ptr (unsigned char *file, size_t file_size, uint32_t rel_off) {

    size_t abs_off;
    
    if (rel_off == INVALID_OFFSET) {
        return 0;
    }
    
    abs_off = BASE_BLOCK_SIZE + rel_off;
    
    if (abs_off + 4 > file_size) {
        return 0;
    }
    
    return file + abs_off;

}

int load_file (const char *ifile, uint8_t **out_buf, size_t *out_size) {

    FILE *fp;
    
    unsigned char *buf;
    long sz;
    
    if (!(fp = fopen (ifile, "rb"))) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to open '%s' for reading", ifile);
        return 0;
    
    }
    
    if (fseek (fp, 0, SEEK_END) != 0) {
    
        fclose (fp);
        return 0;
    
    }
    
    sz = ftell (fp);
    
    if (sz < 0) {
    
        fclose (fp);
        return 0;
    
    }
    
    rewind (fp);
    buf = xmalloc (sz);
    
    if (fread (buf, 1, sz, fp) != (size_t) sz) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed whist reading '%s'", ifile);
        
        free (buf);
        fclose(fp);
        
        return 0;
    
    }
    
    fclose (fp);
    
    *out_buf = buf;
    *out_size = sz;
    
    return 1;

}

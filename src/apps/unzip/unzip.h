/******************************************************************************
 * @file            unzip.h
 *****************************************************************************/
#ifndef     _UNZIP_H
#define     _UNZIP_H

#include    <stdint.h>
#include    "vector.h"

struct unzip_state {

    const char **files;
    long nb_files;
    
    const char *exdir;
    int only_comment, list;
    
    struct vector xlist;

};

extern struct unzip_state *state;
extern const char *program_name;

#define     EXT_ATTR_DIR                (1U << 4)
#define     EXT_ATTR_ARC                (1U << 5)

#define     ZIP_STORE                   0
#define     ZIP_DEFLATE                 8

typedef enum {

    HWINF_OK,                           /* Inflation was successful. */
    HWINF_FULL,                         /* Not enough room in the output buffer. */
    HWINF_ERR                           /* Error in the input data. */

} inf_stat_t;

#include    <stdio.h>
inf_stat_t hwinflate (unsigned char *src, uint64_t src_len, uint64_t *src_used, FILE *outfile, uint64_t dst_cap, uint64_t *dst_used);

#endif      /* _UNZIP_H */

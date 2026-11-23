/******************************************************************************
 * @file            pkg.h
 *****************************************************************************/
#ifndef     _PKG_H
#define     _PKG_H

#include    <stdint.h>

struct pkg_file {

    char *source, *dest;
    
    uint64_t size, offset;
    uint32_t crc32;

};

struct pkg_manifest {

    char *name, *arch;
    char *desc, *version;
    
    struct pkg_file *files;
    uint64_t file_count, file_capacity;

};

struct pkg_header {

    char magic[4];
    uint32_t version;
    
    uint64_t metadata_size;
    uint64_t file_table_size;
    
    uint64_t payload_size;
    uint64_t file_count;
    
    uint32_t control_compression;
    uint64_t control_size;
    
    uint32_t payload_compression;
    uint64_t unpacked_payload_size;

};

#include    <stdio.h>

struct pkg_state {

    const char *ifile, *ofile;
    
    const char **args;
    long nb_args;
    
    struct pkg_manifest manifest;
    FILE *ofp;

};

#define     PKG_MAGIC                   "CPKG"
#define     PKG_VERSION                 3

extern struct pkg_state *state;
extern const char *program_name;

#define     PKG_COMPRESS_NONE           0
#define     PKG_COMPRESS_CHC            1

#endif      /* _PKG_H */

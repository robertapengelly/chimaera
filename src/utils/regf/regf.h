/******************************************************************************
 * @file            regf.h
 *****************************************************************************/
#ifndef     _REGF_H
#define     _REGF_H

#include    <stdio.h>

enum {

    MODE_NONE = 0,
    
    MODE_GENERATE,
    MODE_SET

};

#define     REG_NONE                    0U
#define     REG_SZ                      1U
#define     REG_EXPAND_SZ               2U
#define     REG_BINARY                  3U
#define     REG_DWORD                   4U
#define     REG_MULTI_SZ                7U
#define     REG_QWORD                   11U

enum {

    CELL_TYPE_UNKNOWN = 0,
    CELL_TYPE_NK,
    CELL_TYPE_VK,
    CELL_TYPE_LF,
    CELL_TYPE_SK,
    CELL_TYPE_VALUE_LIST,
    CELL_TYPE_DATA,
    CELL_TYPE_FREE

};

#define     ARRAY_GROW_CAP(cap)         \
    ((cap) < 16 ? 16 : ((cap) * 2))

#define     HBIN_GRANULARITY            4096
#define     HBIN_HEADER_SIZE            32

#define     BASE_BLOCK_SIZE             4096

struct regf_state {

    const char *ifile, *ofile;
    int mode, debug, verbose;
    
    const char **args;
    long nb_args;
    
    FILE *ofp;

};

extern struct regf_state *state;
extern const char *program_name;

#include    <stdint.h>

typedef struct _value {

    char *name;
    uint32_t vk_off, type;
    
    uint8_t *data;
    uint32_t data_len,data_off;
    
    struct _value *next;

} value_t;

typedef struct _key {

    char *name;
    
    struct _key *parent;
    struct _key *children;
    
    value_t *values;
    
    uint32_t subkey_list_off;
    uint32_t value_list_off;
    uint32_t nk_off;
    
    struct _key *next;

} rf_key_t;

typedef struct _stats {

    uint32_t key_count;
    uint32_t value_count;
    uint32_t max_depth;

} stats_t;

typedef struct _cell_info {

    uint32_t off;
    uint32_t size;
    
    int type;

} cell_info_t;

typedef struct _arena {

    uint8_t *buf;
    
    uint32_t used;
    uint32_t cap;
    uint32_t sk_off;
    
    cell_info_t *cells;
    
    uint32_t cell_count;
    uint32_t cell_cap;

} arena_t;

typedef struct _sid_spec {

    uint8_t revision;
    uint8_t subauth_count;
    uint8_t ident_auth[6];
    uint32_t subauth[8];

} sid_spec_t;

#define     INVALID_OFFSET      0xFFFFFFFFU

int create_or_set (void);
int init_parser (void);

uint32_t emit_sk_cell (arena_t *a, uint32_t ref_count);

int parse_binary_hex (char **pp, unsigned char **out_data, uint32_t *out_len);
int parse_input_file (const char *ifile, rf_key_t *root);

int parse_u32_strict (const char *s, uint32_t *out);
int parse_u64_strict (const char *s, uint64_t *out);

unsigned char *hive_rel_ptr(unsigned char *file, size_t file_size, uint32_t rel_off);
unsigned char *get_root_nk(unsigned char *file, size_t file_size);

int load_file (const char *ifile, uint8_t **out_buf, size_t *out_size);

#endif      /* _REGF_H */

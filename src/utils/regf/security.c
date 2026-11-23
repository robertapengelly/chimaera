/******************************************************************************
 * @file            tree.c
 *****************************************************************************/
#include    <stdint.h>
#include    <stdlib.h>
#include    <string.h>

#include    "regf.h"
#include    "arena.h"
#include    "lib.h"

static uint32_t sid_size_bytes (sid_spec_t *sid) {
    return 8 + (uint32_t) sid->subauth_count * 4;
}

static void write_sid (unsigned char *p, sid_spec_t *sid) {

    uint32_t i;
    
    p[0] = sid->revision;
    p[1] = sid->subauth_count;
    
    memcpy (p + 2, sid->ident_auth, 6);
    
    for (i = 0; i < sid->subauth_count; i++) {
        wr32 (p + 8 + i * 4, sid->subauth[i]);
    }

}

static uint32_t ace_size_bytes (sid_spec_t *sid) {
    return 8 + sid_size_bytes (sid);
}

static void write_allow_ace (unsigned char *p, uint8_t flags, uint32_t mask, sid_spec_t *sid) {

    uint32_t ace_size = ace_size_bytes (sid);
    
    p[0] = 0u;                          /* ACCESS_ALLOWED_ACE_TYPE */
    p[1] = flags;
    
    wr16 (p + 2, ace_size);
    wr32 (p + 4, mask);
    
    write_sid (p + 8, sid);

}

static uint32_t build_default_sd (unsigned char **out_sd) {

    static sid_spec_t sid_owner = { 1, 5, { 0, 0, 0, 0, 0, 5}, { 21, 0x4862e393, 0x53fce379, 0x2b3be507, 1003 } };
    static sid_spec_t sid_group = { 1, 5, { 0, 0, 0, 0, 0, 5}, { 21, 0x4862e393, 0x53fce379, 0x2b3be507, 513 } };
    
    static sid_spec_t sid_admins = { 1, 2, { 0, 0, 0, 0, 0, 5}, { 32, 544 } };
    static sid_spec_t sid_system = { 1, 1, { 0, 0, 0, 0, 0, 5}, { 18 } };
    
    static sid_spec_t sid_server_ops = { 1, 1, { 0, 0, 0, 0, 0, 5}, { 12 } };
    
    sid_spec_t *ace_sid[8];
    uint8_t ace_flags[8];
    
    uint32_t ace_mask[8];
    uint32_t ace_size[8];
    uint32_t dacl_size;
    
    uint32_t owner_off, group_off;
    uint32_t sd_len;
    
    unsigned char *sd;
    unsigned char *p;
    
    uint32_t i;
    uint32_t pos;
    
    ace_sid[0] = &sid_owner;      ace_flags[0] = 0x00u; ace_mask[0] = 0x000f003fu;
    ace_sid[1] = &sid_owner;      ace_flags[1] = 0x0bu; ace_mask[1] = 0x10000000u;
    ace_sid[2] = &sid_system;     ace_flags[2] = 0x00u; ace_mask[2] = 0x000f003fu;
    ace_sid[3] = &sid_system;     ace_flags[3] = 0x0bu; ace_mask[3] = 0x10000000u;
    ace_sid[4] = &sid_admins;     ace_flags[4] = 0x00u; ace_mask[4] = 0x000f003fu;
    ace_sid[5] = &sid_admins;     ace_flags[5] = 0x0bu; ace_mask[5] = 0x10000000u;
    ace_sid[6] = &sid_server_ops; ace_flags[6] = 0x00u; ace_mask[6] = 0x00020019u;
    ace_sid[7] = &sid_server_ops; ace_flags[7] = 0x0bu; ace_mask[7] = 0x80000000u;
    
    dacl_size = 8u;
    
    for (i = 0; i < 8u; i++) {
    
        ace_size[i] = ace_size_bytes (ace_sid[i]);
        dacl_size += ace_size[i];
    
    }
    
    owner_off = 20 + dacl_size;
    group_off = owner_off + sid_size_bytes (&sid_owner);
    sd_len = group_off + sid_size_bytes (&sid_group);
    
    sd = xmalloc (sd_len);
    
    /* SECURITY_DESCRIPTOR_RELATIVE */
    sd[0] = 1;
    sd[1] = 0;
    
    wr16 (sd + 2, 0x8004);
    
    wr32 (sd + 4, owner_off);
    wr32 (sd + 8, group_off);
    
    wr32 (sd + 12, 0);
    wr32 (sd + 16, 20);
    
    /* ACL */
    p = sd + 20;
    
    p[0] = 2;
    p[1] = 0;
    
    wr16 (p + 2, dacl_size);
    
    wr16 (p + 4, 8);
    wr16 (p + 6, 0);
    
    pos = 8;
    
    for (i = 0; i < 8; i++) {
    
        write_allow_ace (p + pos, ace_flags[i], ace_mask[i], ace_sid[i]);
        pos += ace_size[i];
    
    }
    
    write_sid (sd + owner_off, &sid_owner);
    write_sid (sd + group_off, &sid_group);
    
    *out_sd = sd;
    return sd_len;

}

uint32_t emit_sk_cell (arena_t *a, uint32_t ref_count) {

    uint32_t sd_len;
    uint8_t *sd;
    
    uint32_t payload_len;
    uint32_t off;
    
    uint8_t *p;
    
    sd_len = build_default_sd (&sd);
    payload_len = 20 + sd_len;
    
    off = arena_alloc_cell (a, payload_len, CELL_TYPE_SK);
    p = arena_ptr (a, off) + 4;
    
    memcpy (p + 0, "sk", 2);
    
    wr16 (p + 2, 0);
    wr32 (p + 4, off);
    wr32 (p + 8, off);
    
    wr32 (p + 12, ref_count);
    wr32 (p + 16, sd_len);
    
    memcpy (p + 20, sd, sd_len);
    free (sd);
    
    return off;

}

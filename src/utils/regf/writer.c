/******************************************************************************
 * @file            writer.c
 *****************************************************************************/
#include    <stdlib.h>
#include    <string.h>

#include    "regf.h"
#include    "arena.h"
#include    "tree.h"
#include    "lib.h"
#include    "report.h"
#include    "writer.h"

static void patch_sk_links (arena_t *a, rf_key_t *k) {

    unsigned char *p;
    rf_key_t *c;
    
    if (k->nk_off == INVALID_OFFSET) {
        return;
    }
    
    p = arena_ptr (a, k->nk_off) + 4;
    wr32 (p + 44, a->sk_off);
    
    for (c = k->children; c; c = c->next) {
        patch_sk_links (a, c);
    }

}

static uint32_t count_keys_recursive (rf_key_t *k) {

    uint32_t n = 1u;
    rf_key_t *c;
    
    for (c = k->children; c; c = c->next) {
        n += count_keys_recursive (c);
    }
    
    return n;

}

static void emit_value (arena_t *a, value_t *v) {

    uint32_t name_len = strlen (v->name);
    uint32_t vk_payload = 20 + name_len;
    
    uint32_t off;
    uint8_t *p;
    
    if (v->data_len > 4) {
    
        uint32_t data_cell = arena_alloc_cell (a, v->data_len, CELL_TYPE_DATA);
        
        uint8_t *dp = arena_ptr (a, data_cell) + 4;
        memcpy (dp, v->data, v->data_len);
        
        v->data_off = data_cell;
    
    }
    
    off = arena_alloc_cell (a, vk_payload, CELL_TYPE_VK);
    
    v->vk_off = off;
    p = arena_ptr (a, off) + 4;
    
    memcpy (p + 0, "vk", 2);
    wr16 (p + 2, name_len);
    
    if (v->data_len <= 4u) {
    
        uint32_t size_field = v->data_len | 0x80000000u;
        
        uint8_t tmp[4] = {0, 0, 0, 0};
        memcpy (tmp, v->data, v->data_len);
        
        wr32 (p + 4, size_field);
        memcpy (p + 8, tmp, 4u);
    
    } else {
    
        wr32 (p + 4, v->data_len);
        wr32 (p + 8, v->data_off);
    
    }
    
    wr32 (p + 12, v->type);
    
    wr16 (p + 16, name_len ? 1 : 0);
    wr16 (p + 18, 0);
    
    memcpy (p + 20, v->name, name_len);

}

static void emit_values_for_key (arena_t *a, rf_key_t *k) {

    uint32_t n = count_values (k);
    uint32_t i = 0;
    
    uint32_t off;
    uint8_t *p;
    
    value_t *v;
    
    for (v = k->values; v; v = v->next) {
        emit_value (a, v);
    }
    
    if (n == 0) {
    
        k->value_list_off = INVALID_OFFSET;
        return;
    
    }
    
    off = arena_alloc_cell (a, n * 4, CELL_TYPE_VALUE_LIST);
    
    k->value_list_off = off;
    p = arena_ptr (a, off) + 4;
    
    for (v = k->values; v; v = v->next, i++) {
        wr32 (p + i * 4, v->vk_off);
    }

}

static void emit_subkey_list_for_key (arena_t *a, rf_key_t *k) {

    uint32_t n = count_children (k);
    uint32_t i = 0;
    
    uint32_t off;
    uint8_t *p;
    
    size_t j;
    rf_key_t *c;
    
    if (n == 0) {
    
        k->subkey_list_off = INVALID_OFFSET;
        return;
    
    }
    
    off = arena_alloc_cell (a, 4 + n * 8, CELL_TYPE_LF);
    
    k->subkey_list_off = off;
    p = arena_ptr (a, off) + 4;
    
    memcpy (p + 0, "lf", 2);
    wr16 (p + 2, n);
    
    for (c = k->children; c; c = c->next, i++) {
    
        wr32 (p + 4 + i * 8, c->nk_off);
        memset (p + 8 + i * 8, 0, 4);
        
        for (j = 0; j < 4u && c->name[j] != '\0'; j++) {
        
            unsigned char ch = c->name[j];
            
            if (ch >= 'a' && ch <= 'z') {
                ch = (ch - 'a' + 'A');
            }
            
            p[8 + i * 8 + j] = ch;
        
        }
    
    }

}

#define     KEY_ROOT_FLAG               0x002c
#define     KEY_NORMAL_FLAG             0x0020

static void emit_key_recursive (arena_t *a, rf_key_t *k) {

    uint32_t name_len = strlen (k->name);
    uint32_t nk_payload = 76 + name_len;
    
    uint8_t *p;
    uint32_t nsub, nval;
    
    rf_key_t *c;
    sort_children (k);
    
    for (c = k->children; c; c = c->next) {
        emit_key_recursive (a, c);
    }
    
    emit_values_for_key (a, k);
    emit_subkey_list_for_key (a, k);
    
    k->nk_off = arena_alloc_cell (a, nk_payload, CELL_TYPE_NK);
    p = arena_ptr (a, k->nk_off) + 4;
    
    nsub = count_children (k);
    nval = count_values (k);
    
    memcpy (p, "nk", 2);
    
    wr16 (p + 2, (k->parent ? KEY_NORMAL_FLAG : KEY_ROOT_FLAG));
    wr64 (p + 4, unix_time_to_filetime (time (0)));
    wr32 (p + 12, 0);
    wr32 (p + 16, k->parent ? k->parent->nk_off : INVALID_OFFSET);
    wr32 (p + 20, nsub);
    wr32 (p + 24, 0);
    wr32 (p + 28, k->subkey_list_off);
    wr32 (p + 32, INVALID_OFFSET);
    wr32 (p + 36, nval);
    wr32 (p + 40, k->value_list_off);
    wr32 (p + 44, a->sk_off);
    wr32 (p + 48, INVALID_OFFSET);
    wr32 (p + 52, max_subkey_name_u16 (k));
    wr32 (p + 56, 0);
    wr32 (p + 60, max_value_name_u16 (k));
    wr32 (p + 64, max_value_data_size (k));
    wr32 (p + 68, 0);
    wr16 (p + 72, name_len);
    wr16 (p + 74, 0);
    
    memcpy (p + 76, k->name, name_len);

}

static void patch_parent_links (arena_t *a, rf_key_t *k) {

    uint8_t *p;
    rf_key_t *c;
    
    if (k->nk_off == INVALID_OFFSET) {
        return;
    }
    
    p = arena_ptr (a, k->nk_off) + 4u;
    
    if (k->parent) {
        wr32 (p + 16u, k->parent->nk_off);
    } else {
        wr32 (p + 16u, INVALID_OFFSET);
    }
    
    for (c = k->children; c; c = c->next) {
        patch_parent_links (a, c);
    }

}

static int offset_in_hive (uint32_t off, uint32_t hive_size) {
    return off >= HBIN_HEADER_SIZE && off < hive_size;
}

static int validate_emitted_key (arena_t *a, rf_key_t *k, uint32_t hive_size) {

    rf_key_t *c;
    value_t *v;
    
    unsigned char *nkp;
    cell_info_t *cell;
    
    uint32_t nsub, nval;
    
    if (k->nk_off == INVALID_OFFSET) {
    
        report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' missing nk offset", k->name);
        return 0;
    
    }
    
    if (!offset_in_hive(k->nk_off, hive_size)) {
    
        report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' nk offset out of range", k->name);
        return 0;
    
    }
    
    cell = find_cell (a, k->nk_off);
    
    if (!cell || cell->type != CELL_TYPE_NK) {
    
        report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' nk cell not found", k->name);
        return 0;
    
    }
    
    nkp = arena_ptr (a, k->nk_off) + 4;
    
    if (memcmp (nkp, "nk", 2) != 0) {
    
        report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' nk signature invalid", k->name);
        return 0;
    
    }
    
    {
    
        uint32_t sk_off = rd32 (nkp + 44);
        
        if (sk_off != a->sk_off) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' security offset mismatch", k->name);
            return 0;
        
        }
    
    }
    
    {
    
        uint32_t parent_off = rd32 (nkp + 16);
        uint32_t expected = k->parent ? k->parent->nk_off : INVALID_OFFSET;
        
        if (parent_off != expected) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' parent offset mismatch", k->name);
            return 0;
        
        }
    
    }
    
    if (k->parent) {
    
        if (rd32 (nkp + 16) != k->parent->nk_off) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' parent offset mismatch", k->name);
            return 0;
        
        }
    
    }
    
    nsub = rd32 (nkp + 20);
    nval = rd32 (nkp + 36);
    
    if (nsub != count_children (k)) {
    
        report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' subkey count mismatch", k->name);
        return 0;
    
    }
    
    if (nval != count_values(k)) {
    
        report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' value count mismatch", k->name);
        return 0;
    
    }
    
    if (count_children(k) == 0u) {
    
        if (rd32 (nkp + 28) != INVALID_OFFSET) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' should not have subkey list", k->name);
            return 0;
        
        }
    
    } else {
    
        uint32_t lfoff = rd32 (nkp + 28);
        unsigned char *lfp;
        
        if (!offset_in_hive (lfoff, hive_size)) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' lf offset invalid", k->name);
            return 0;
        
        }
        
        cell = find_cell (a, lfoff);
        
        if (!cell || cell->type != CELL_TYPE_LF) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' lf cell not found", k->name);
            return 0;
        
        }
        
        lfp = arena_ptr (a, lfoff) + 4;
        
        if (memcmp (lfp, "lf", 2) != 0) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' lf signature invalid", k->name);
            return 0;
        
        }
        
        if (rd16 (lfp + 2) != count_children (k)) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' lf count mismatch", k->name);
            return 0;
        
        }
    
    }
    
    if (count_values (k) == 0) {
    
        if (rd32 (nkp + 40) != INVALID_OFFSET) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' should not have value list", k->name);
            return 0;
        
        }
    
    } else {
    
        uint32_t vloff = rd32 (nkp + 40);
        
        if (!offset_in_hive (vloff, hive_size)) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' value list offset invalid", k->name);
            return 0;
        
        }
        
        cell = find_cell(a, vloff);
        
        if (!cell || cell->type != CELL_TYPE_VALUE_LIST) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: key '%s' value list cell not found", k->name);
            return 0;
        
        }
    
    }
    
    for (v = k->values; v; v = v->next) {
    
        unsigned char *vkp;
        
        if (!offset_in_hive (v->vk_off, hive_size)) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: value '%s' in key '%s' vk offset invalid", v->name, k->name);
            return 0;
        
        }
        
        cell = find_cell (a, v->vk_off);
        
        if (!cell || cell->type != CELL_TYPE_VK) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: value '%s' in key '%s' vk cell not found", v->name, k->name);
            return 0;
        
        }
        
        vkp = arena_ptr (a, v->vk_off) + 4;
        
        if (memcmp (vkp, "vk", 2) != 0) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: value '%s' in key '%s' vk signature invalid", v->name, k->name);
            return 0;
        
        }
        
        if (rd32 (vkp + 12) != v->type) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: value '%s' in key '%s' type mismatch", v->name, k->name);
            return 0;
        
        }
        
        if (v->data_len > 4u) {
        
            uint32_t dsz = rd32 (vkp + 4);
            uint32_t doff = rd32 (vkp + 8);
            
            if (dsz != v->data_len) {
            
                report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: value '%s' in key '%s' data size mismatch", v->name, k->name);
                return 0;
            
            }
            
            if (!offset_in_hive (doff, hive_size)) {
            
                report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: value '%s' in key '%s' data offset invalid", v->name, k->name);
                return 0;
            
            }
            
            cell = find_cell (a, doff);
            
            if (!cell || cell->type != CELL_TYPE_DATA) {
            
                report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: value '%s' in key '%s' data cell missing", v->name, k->name);
                return 0;
            
            }
        
        }
    
    }
    
    for (c = k->children; c; c = c->next) {
    
        if (!validate_emitted_key (a, c, hive_size)) {
            return 0;
        }
    
    }
    
    return 1;

}

static int validate_hive_serialization (arena_t *a, rf_key_t *root, uint32_t hive_size) {

    uint32_t i;
    
    if (root->nk_off == INVALID_OFFSET) {
    
        report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: root key missing nk offset");
        return 0;
    
    }
    
    for (i = 0; i < a->cell_count; i++) {
    
        cell_info_t *ci = &a->cells[i];
        
        if ((ci->off & 7) != 0) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: cell offset 0x%08x is not 8-byte aligned", ci->off);
            return 0;
        
        }
        
        if ((ci->size & 7) != 0 || ci->size < 8) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: cell size at 0x%08x invalid", ci->off);
            return 0;
        
        }
        
        if (ci->off + ci->size > hive_size) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: cell at 0x%08x exceeds hive size", ci->off);
            return 0;
        
        }
    
    }
    
    {
        cell_info_t *ci = find_cell (a, a->sk_off);
        uint32_t expected_sk_size;
        
        if (!ci || ci->type != CELL_TYPE_SK) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: security cell missing");
            return 0;
        
        }
        
        /* sk payload = 20-byte sk header + actual sd blob length */
        expected_sk_size = ci->size;
        
        if (ci->size != expected_sk_size) {
        
            report_at (program_name, 0, REPORT_ERROR, "serialization validation failed: security cell size mismatch (got %u expected %u)", ci->size, expected_sk_size);
            return 0;
        
        }
    
    }
    
    return validate_emitted_key (a, root, hive_size);

}

static void write_hbin_header (unsigned char *dst, uint32_t hbin_off, uint32_t hbin_size) {

    memset (dst, 0, HBIN_HEADER_SIZE);
    memcpy (dst + 0, "hbin", 4);
    
    wr32 (dst + 4, hbin_off);
    wr32 (dst + 8, hbin_size);

}

static uint32_t baseblock_checksum (unsigned char *base508) {

    uint32_t c = 0;
    size_t i;
    
    for (i = 0; i < 508; i += 4u) {
        c ^= rd32 (base508 + i);
    }
    
    if (c == 0xFFFFFFFFU) {
        c = 0xFFFFFFFEU;
    }
    
    if (c == 0u) {
        c = 1;
    }
    
    return c;

}

static void write_base_block (unsigned char *dst, uint32_t root_off, uint32_t hive_bins_size, const char *name_hint) {

    uint64_t ft = unix_time_to_filetime (time (0));
    size_t i, n;
    
    memset (dst, 0, BASE_BLOCK_SIZE);
    memcpy (dst + 0, "regf", 4);
    
    wr32 (dst + 4, 1);
    wr32 (dst + 8, 1);
    
    wr64 (dst + 12, ft);
    wr32 (dst + 20, 1);
    wr32 (dst + 24, 4);
    wr32 (dst + 28, 0);
    wr32 (dst + 32, 1);
    
    wr32 (dst + 36, root_off);
    wr32 (dst + 40, hive_bins_size);
    
    wr32 (dst + 44, 1);
    
    n = strlen (name_hint);
    
    if (n > 31) {
        n = 31;
    }
    
    for (i = 0; i < n; i++) {
    
        dst[48 + i * 2] = name_hint[i];
        dst[48 + i * 2 + 1] = 0;
    
    }
    
    wr32 (dst + 508, baseblock_checksum (dst));

}

int write_hive_file (const char *ofile, rf_key_t *root) {

    unsigned char *hive_data;
    unsigned char *base;
    
    uint32_t data_with_headers;
    uint32_t hb_count;
    uint32_t total_hive_size;
    
    uint32_t remaining;
    uint32_t pos;
    uint32_t i;
    
    arena_t a = { 0 };
    init_arena (&a, 8192);
    
    emit_key_recursive (&a, root);
    a.sk_off = emit_sk_cell (&a, count_keys_recursive (root));
    
    patch_parent_links (&a, root);
    patch_sk_links (&a, root);
    
    data_with_headers = HBIN_HEADER_SIZE + a.used;
    hb_count = (data_with_headers + HBIN_GRANULARITY - 1) / HBIN_GRANULARITY;
    
    if (hb_count == 0) {
        hb_count = 1;
    }
    
    hive_data = xmalloc ((total_hive_size = hb_count * HBIN_GRANULARITY));
    
    for (i = 0; i < hb_count; i++) {
    
        uint32_t this_hbin_off = i * HBIN_GRANULARITY;
        write_hbin_header (hive_data + this_hbin_off, this_hbin_off, HBIN_GRANULARITY);
    
    }
    
    memcpy (hive_data + HBIN_HEADER_SIZE, a.buf, a.used);
    pos = HBIN_HEADER_SIZE + a.used;
    
    while (pos < total_hive_size) {
    
        uint32_t bin_start = (pos / HBIN_GRANULARITY) * HBIN_GRANULARITY;
        uint32_t bin_end = bin_start + HBIN_GRANULARITY;
        uint32_t min_payload_pos = bin_start + HBIN_HEADER_SIZE;
        
        if (pos < min_payload_pos) {
            pos = min_payload_pos;
        }
        
        if (pos >= bin_end) {
        
            pos = bin_end;
            continue;
        
        }
        
        remaining = bin_end - pos;
        
        if (remaining >= 4u) {
        
            wr32 (hive_data + pos, remaining);
            pos = bin_end;
        
        } else {
            break;
        }
    
    }
    
    if (!validate_hive_serialization (&a, root, total_hive_size)) {
        return 0;
    }
    
    base = xmalloc (BASE_BLOCK_SIZE);
    write_base_block (base, root->nk_off, total_hive_size, "");
    
    if (!(state->ofp = fopen (ofile, "wb"))) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to open '%s' for writing", ofile);
        return 0;
    
    }
    
    if (fwrite (base, 1, BASE_BLOCK_SIZE, state->ofp) != BASE_BLOCK_SIZE) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to write base block to '%s'", ofile);
        return 0;
    
    }
    
    if (fwrite (hive_data, 1, total_hive_size, state->ofp) != total_hive_size) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to write hive data to '%s'", ofile);
        return 0;
    }
    
    fclose (state->ofp);
    state->ofp = 0;
    
    if (state->verbose) {
    
        printf ("wrote hive: %s\n", ofile);
        printf ("\n");
        printf ("    root nk offset : 0x%08x\n", root->nk_off);
        printf ("    data bytes     : %u\n", a.used);
        printf ("    hbin count     : %u\n", hb_count);
        printf ("    hive bins size : %u\n", total_hive_size);
        printf ("    cell count     : %u\n", a.cell_count);
    
    }
    
    return 1;

}

void validate_tree_rec (const rf_key_t *k) {

    rf_key_t *a, *b;
    value_t *v1, *v2;
    
    for (a = k->children; a; a = a->next) {
    
        if (!a->name || !a->name[0]) {
        
            report_at (program_name, 0, REPORT_ERROR, "empty child key name under '%s'", k->name);
            continue;
        
        }
        
        for (b = a->next; b; b = b->next) {
        
            if (strcmp (a->name, b->name) == 0) {
            
                report_at (program_name, 0, REPORT_ERROR, "duplicate subkey '%s' under '%s'", a->name, k->name);
                continue;
            
            }
        
        }
        
        validate_tree_rec (a);
    
    }
    
    for (v1 = k->values; v1; v1 = v1->next) {
    
        if (!v1->name) {
        
            report_at (program_name, 0, REPORT_ERROR, "invalid value name in key '%s'", k->name);
            continue;
        
        }
        
        for (v2 = v1->next; v2; v2 = v2->next) {
        
            if (strcmp (v1->name, v2->name) == 0) {
            
                report_at (program_name, 0, REPORT_ERROR, "duplicate value '%s' in key '%s'", v1->name, k->name);
                continue;
            
            }
        
        }
    
    }

}

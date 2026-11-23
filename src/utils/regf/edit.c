/******************************************************************************
 * @file            edit.c
 *****************************************************************************/
#include    <stdlib.h>
#include    <string.h>

#include    "regf.h"
#include    "lib.h"
#include    "tree.h"
#include    "report.h"
#include    "writer.h"

static value_t *build_value_from_vk (unsigned char *file, size_t file_size, unsigned char *vk) {

    uint16_t name_len = rd16 (vk + 2u);
    uint32_t size_field = rd32 (vk + 4u);
    
    uint32_t data_len = size_field & 0x7fffffffu;
    uint32_t type = rd32 (vk + 12u);
    
    unsigned char *tmp_data = 0;
    unsigned char *src = 0;
    
    char *name;
    value_t *v;
    
    name = xmalloc ((size_t) name_len + 1);
    
    memcpy (name, vk + 20, name_len);
    name[name_len] = 0;
    
    if (size_field & 0x80000000U) {
        src = vk + 8;
    } else {
    
        uint32_t data_off = rd32 (vk + 8);
        unsigned char *datacell = hive_rel_ptr (file, file_size, data_off);
        
        if (!datacell) {
        
            free (name);
            return 0;
        
        }
        
        src = datacell + 4;
    
    }
    
    if (data_len) {
    
        tmp_data = xmalloc (data_len);
        memcpy (tmp_data, src, data_len);
    
    }
    
    { unsigned char zero = 0; v = value_new (name, type, tmp_data ? tmp_data : &zero, data_len); }
    
    free (name);
    free (tmp_data);
    
    return v;

}

static int load_values_into_key (unsigned char *file, size_t file_size, unsigned char *nk, rf_key_t *k) {

    uint32_t nval = rd32 (nk + 36);
    uint32_t vloff = rd32 (nk + 40);
    
    unsigned char *vlcell, *vl;
    uint32_t i;
    
    if (nval == 0 || vloff == INVALID_OFFSET) {
        return 1;
    }
    
    vlcell = hive_rel_ptr (file, file_size, vloff);
    
    if (!vlcell) {
        return 0;
    }
    
    vl = vlcell + 4;
    
    for (i = 0; i < nval; i++) {
    
        uint32_t vkoff = rd32 (vl + i * 4u);
        
        unsigned char *vkcell = hive_rel_ptr (file, file_size, vkoff);
        unsigned char *vk;
        
        value_t *v;
        
        if (!vkcell) {
            return 0;
        }
        
        vk = vkcell + 4;
        
        if (memcmp (vk, "vk", 2) != 0) {
            return 0;
        }
        
        v = build_value_from_vk (file, file_size, vk);
        
        if (!v) {
            return 0;
        }
        
        add_value_node (k, v);
    
    }
    
    return 1;

}

static rf_key_t *build_tree_from_nk (unsigned char *file, size_t file_size, unsigned char *nk, rf_key_t *parent) {

    uint16_t name_len = rd16 (nk + 72);
    
    char *name;
    rf_key_t *k;
    
    uint32_t nsub = rd32 (nk + 20);
    uint32_t lfoff = rd32 (nk + 28);
    uint32_t i;
    
    name = xmalloc ((size_t) name_len + 1);
    
    memcpy (name, nk + 76, name_len);
    name[name_len] = 0;
    
    k = key_new (name, parent);
    free (name);
    
    if (!load_values_into_key (file, file_size, nk, k)) {
        return 0;
    }
    
    if (nsub != 0u && lfoff != INVALID_OFFSET) {
    
        unsigned char *lfcell = hive_rel_ptr (file, file_size, lfoff);
        unsigned char *lf;
        
        if (!lfcell) {
            return 0;
        }
        
        lf = lfcell + 4;
        
        if (memcmp (lf, "lf", 2) != 0) {
            return 0;
        }
        
        for (i = 0; i < nsub; i++) {
        
            uint32_t child_off = rd32 (lf + 4 + i * 8);
            
            unsigned char *child_cell = hive_rel_ptr (file, file_size, child_off);
            unsigned char *child_nk;
            
            rf_key_t *child;
            rf_key_t *tail;
            
            if (!child_cell) {
                return 0;
            }
            
            child_nk = child_cell + 4;
            
            if (memcmp (child_nk, "nk", 2) != 0) {
                return 0;
            }
            
            child = build_tree_from_nk (file, file_size, child_nk, k);
            
            if (!child) {
                return 0;
            }
            
            if (!k->children) {
                k->children = child;
            } else {
            
                tail = k->children;
                
                while (tail->next) {
                    tail = tail->next;
                }
                
                tail->next = child;
            
            }
        
        }
    
    }
    
    return k;

}

static rf_key_t *load_tree_from_hive (const char *hive_path) {

    unsigned char *file;
    size_t file_size;
    
    unsigned char *root_nk;
    rf_key_t *root;
    
    if (!load_file (hive_path, &file, &file_size)) {
        return 0;
    }
    
    root_nk = get_root_nk (file, file_size);
    
    if (!root_nk) {
    
        free (file);
        return 0;
    
    }
    
    root = build_tree_from_nk (file, file_size, root_nk, 0);
    free (file);
    
    return root;

}

static rf_key_t *find_or_create_key (rf_key_t *root, const char *path) {

    char *tmp, *part, *next;
    rf_key_t *cur;
    
    if (!path || !*path) {
        return root;
    }
    
    cur = root;
    
    tmp = xstrdup (path);
    part = tmp;
    
    while (*part == '\\') {
        part++;
    }
    
    if (*part) {
    
        char *end = part;
        
        while (*end && *end != '\\') {
            end++;
        }
        
        if ((size_t) (end - part) == strlen (cur->name) && memcmp (cur->name, part, (size_t) (end - part)) == 0) {
        
            part = end;
            
            while (*part == '\\') {
                part++;
            }
        
        }
    
    }
    
    if (!*part) {
    
        free (tmp);
        return cur;
    
    }
    
    while (*part) {
    
        next = part;
        
        while (*next && *next != '\\') {
            next++;
        }
        
        if (*next) {
        
            *next = 0;
            
            cur = append_child (cur, part);
            part = next + 1;
            
            while (*part == '\\') {
                part++;
            }
        
        } else {
        
            cur = append_child(cur, part);
            break;
        
        }
    
    }
    
    free (tmp);
    return cur;

}

static void set_value (rf_key_t *k, const char *name, uint32_t type, uint8_t *data, uint32_t data_len) {

    value_t *v;
    
    name = canonical_value_name (name);
    v = find_value (k->values, name);
    
    if (v) {
    
        free (v->data);
        v->data = xmalloc (data_len ? data_len : 1);
        
        if (data_len) {
            memcpy (v->data, data, data_len);
        }
        
        v->data_len = data_len;
        v->type = type;
        
        return;
    
    }
    
    add_value_node (k, value_new (name, type, data, data_len));

}

static int parse_type_and_data (const char *type_s, const char *value_s, uint32_t *out_type, unsigned char **out_data,  uint32_t *out_len) {

    unsigned char *data = 0;
    int ret;
    
    uint32_t size = 0, type = 0;
    
    char *tmp = (char *) value_s;
    
    if (ascii_casecmp (type_s, "REG_SZ") == 0 || ascii_casecmp (type_s, "SZ") == 0) {
    
        data = ascii_to_utf16le (&tmp, &size);
        type = REG_SZ;
    
    } else if (ascii_casecmp (type_s, "REG_EXPAND_SZ") == 0 || ascii_casecmp (type_s, "EXPAND") == 0) {
    
        data = ascii_to_utf16le (&tmp, &size);
        type = REG_EXPAND_SZ;
    
    } else if (ascii_casecmp (type_s, "REG_DWORD") == 0 || ascii_casecmp (type_s, "DWORD") == 0) {
    
        uint32_t v;
        
        if (!parse_u32_strict (value_s, &v)) {
        
            report_at (program_name, 0, REPORT_FATAL_ERROR, "invalid dword '%s'", value_s);
            return 0;
        
        }
        
        data = xmalloc (4);
        wr32 (data, v);
        
        type = REG_DWORD;
        size = 4;
    
    } else if (ascii_casecmp (type_s, "REG_QWORD") == 0 || ascii_casecmp (type_s, "QWORD") == 0) {
    
        uint64_t v;
        
        if (!parse_u64_strict (value_s, &v)) {
        
            report_at (program_name, 0, REPORT_FATAL_ERROR, "invalid qword '%s'", value_s);
            return 0;
        
        }
        
        data = xmalloc(8u);
        wr64 (data, v);
        
        type = REG_QWORD;
        size = 8;
    
    } else if (ascii_casecmp (type_s, "REG_BINARY") == 0 || ascii_casecmp (type_s, "BINARY") == 0) {
    
        if ((ret = parse_binary_hex (&tmp, &data, &size))) {
        
            if (ret < 0) {
            
                report_at (program_name, 0, REPORT_ERROR, "invalid hex digit '%c' in binary", *tmp);
                return 0;
            
            }
            
            report_at (program_name, 0, REPORT_ERROR, "odd number '%x' of hex digits", ret);
            return 0;
        
        }
        
        type = REG_BINARY;
    
    } else {
    
        report_at (program_name, 0, REPORT_FATAL_ERROR, "unsupported type '%s'", type_s);
        return 0;
    
    }
    
    *out_type = type;
    *out_data = data;
    *out_len = size;
    
    return 1;

}

int create_or_set (void) {

    rf_key_t *root, *k;
    
    uint32_t type, data_len;
    uint8_t *data;
    
    root = load_tree_from_hive (state->args[0]);
    
    if (!root) {
    
        report_at (program_name, 0, REPORT_FATAL_ERROR, "failed to load hive '%s'", state->args[0]);
        return 1;
    
    }
    
    if (!parse_type_and_data (state->args[3], state->args[4], &type, &data, &data_len)) {
        return 1;
    }
    
    k = find_or_create_key (root, state->args[1]);
    set_value (k, state->args[2], type, data, data_len);
    
    free (data);
    
    if (!write_hive_file (state->args[0], root)) {
        return 0;
    }
    
    return 1;

}

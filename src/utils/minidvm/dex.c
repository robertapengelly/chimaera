/******************************************************************************
 * @file            dex.c
 *****************************************************************************/
#include "mini_dalvik_vm.h"

u2 dex_read_u2 (const DexFile *dex, u4 off) {

    if (dex == NULL || dex->data == NULL || off + 2 > dex->size) {
        return 0;
    }
    
    return (u2) (((u2) dex->data[off]) | ((u2) dex->data[off + 1] << 8));

}

u4 dex_read_u4 (const DexFile *dex, u4 off) {

    if (dex == NULL || dex->data == NULL || off + 4 > dex->size) {
        return 0;
    }
    
    return ((u4) dex->data[off]) | ((u4) dex->data[off + 1] << 8) | ((u4) dex->data[off + 2] << 16) | ((u4) dex->data[off + 3] << 24);

}

int dex_bounds_ok (const DexFile *dex, u4 off, u4 size) {

    if (dex == NULL || dex->data == NULL) {
        return 0;
    }
    
    if (off > dex->size) {
        return 0;
    }
    
    if (size > dex->size - off) {
        return 0;
    }
    
    return 1;

}

static int dex_is_magic (const u1 *magic) {
    return magic[0] == 'd' && magic[1] == 'e' && magic[2] == 'x' && magic[3] == '\n' && magic[7] == '\0';
}

int dex_read_uleb128 (const DexFile *dex, u4 *off_io, u4 *out) {

    u4 result;
    u4 shift;
    u4 off;
    u1 byte;
    
    if (dex == NULL || off_io == NULL || out == NULL) {
        return 0;
    }
    
    result = 0;
    shift = 0;
    
    off = *off_io;
    
    while (1) {
    
        if (!dex_bounds_ok (dex, off, 1)) {
            return 0;
        }
        
        byte = dex->data[off++];
        result |= ((u4)(byte & 0x7f)) << shift;
        
        if ((byte & 0x80) == 0) {
            break;
        }
        
        shift += 7;
        
        if (shift > 28) {
            return 0;
        }
    
    }
    
    *off_io = off;
    *out = result;
    
    return 1;

}

int dex_get_type_descriptor (const DexFile *dex, u4 type_idx, const char **out);

static int dex_read_sleb128 (const DexFile *dex, u4 *off_io, s4 *out) {

    s4 result;
    u4 shift;
    u4 off;
    u1 byte;
    
    if (dex == NULL || off_io == NULL || out == NULL) {
        return 0;
    }
    
    result = 0;
    shift = 0;
    
    off = *off_io;
    byte = 0;
    
    do {
    
        if (!dex_bounds_ok (dex, off, 1) || shift >= 32) {
            return 0;
        }
        
        byte = dex->data[off++];
        
        result |= (s4)((u4)(byte & 0x7f) << shift);
        shift += 7;
    
    } while ((byte & 0x80) != 0);
    
    if (shift < 32 && (byte & 0x40) != 0) {
        result |= (s4)(~0U << shift);
    }
    
    *off_io = off;
    *out = result;
    
    return 1;

}

int dex_find_exception_handler (const DexFile *dex, u4 code_off, u4 throw_pc, const char *exception_desc, u4 *handler_pc) {

    u2 tries_size;
    u4 insns_size;
    u4 tries_off;
    u4 handlers_off;
    u4 i;
    
    if (dex == NULL || exception_desc == NULL || handler_pc == NULL) {
        return 0;
    }
    
    tries_size = dex_read_u2 (dex, code_off + 6);
    insns_size = dex_read_u4 (dex, code_off + 12);
    
    if (tries_size == 0) {
        return 0;
    }
    
    tries_off = code_off + 16 + insns_size * 2;
    
    if ((insns_size & 1U) != 0) {
        tries_off += 2;
    }
    
    handlers_off = tries_off + (u4)tries_size * 8;
    
    if (!dex_bounds_ok (dex, tries_off, (u4)tries_size * 8) || !dex_bounds_ok (dex, handlers_off, 1)) {
        return 0;
    }
    
    for (i = 0; i < tries_size; i++) {
    
        u4 item_off;
        u4 start_addr;
        u2 insn_count;
        u2 handler_rel;
        u4 off;
        s4 count;
        u4 typed_count;
        u4 j;
        u4 catch_all_addr;
        
        int has_catch_all;
        item_off = tries_off + i * 8;
        
        start_addr = dex_read_u4 (dex, item_off);
        insn_count = dex_read_u2 (dex, item_off + 4);
        
        handler_rel = dex_read_u2 (dex, item_off + 6);
        
        if (throw_pc < start_addr || throw_pc >= start_addr + insn_count) {
            continue;
        }
        
        off = handlers_off + handler_rel;
        
        if (!dex_read_sleb128 (dex, &off, &count)) {
            return 0;
        }
        
        has_catch_all = count <= 0;
        
        typed_count = (u4) (count < 0 ? -count : count);
        catch_all_addr = 0;
        
        for (j = 0; j < typed_count; j++) {
        
            u4 type_idx;
            u4 addr;
            
            const char *handler_desc;
            
            if (!dex_read_uleb128 (dex, &off, &type_idx) || !dex_read_uleb128 (dex, &off, &addr) || !dex_get_type_descriptor (dex, type_idx, &handler_desc)) {
                return 0;
            }
            
            if (strcmp (handler_desc, exception_desc) == 0) {
            
                *handler_pc = addr;
                return 1;
            
            }
        
        }
        
        if (has_catch_all) {
        
            if (!dex_read_uleb128 (dex, &off, &catch_all_addr)) {
                return 0;
            }
            
            *handler_pc = catch_all_addr;
            return 1;
        
        }
        
        return 0;
    
    }
    
    return 0;

}

int dex_get_string (const DexFile *dex, u4 string_idx, const char **out) {

    u4 string_data_off;
    u4 off;
    u4 dummy_len;
    
    if (dex == NULL || out == NULL || string_idx >= dex->string_ids_size) {
        return 0;
    }
    
    string_data_off = dex_read_u4 (dex, dex->string_ids_off + string_idx * 4);
    off = string_data_off;
    
    if (!dex_read_uleb128 (dex, &off, &dummy_len)) {
        return 0;
    }
    
    if (!dex_bounds_ok (dex, off, 1)) {
        return 0;
    }
    
    *out = (const char *) (dex->data + off);
    return 1;

}

int dex_get_type_descriptor (const DexFile *dex, u4 type_idx, const char **out) {

    u4 string_idx;
    
    if (dex == NULL || out == NULL || type_idx >= dex->type_ids_size) {
        return 0;
    }
    
    string_idx = dex_read_u4 (dex, dex->type_ids_off + type_idx * 4);
    return dex_get_string (dex, string_idx, out);

}

static int dex_get_method_id (const DexFile *dex, u4 method_idx, DexMethodId *out) {

    u4 off;
    
    if (dex == NULL || out == NULL || method_idx >= dex->method_ids_size) {
        return 0;
    }
    
    off = dex->method_ids_off + method_idx * 8;
    
    if (!dex_bounds_ok (dex, off, 8)) {
        return 0;
    }
    
    out->class_idx = dex_read_u2 (dex, off + 0);
    out->proto_idx = dex_read_u2 (dex, off + 2);
    out->name_idx  = dex_read_u4 (dex, off + 4);
    
    return 1;

}

int dex_get_field_id (const DexFile *dex, u4 field_idx, DexFieldId *out) {

    u4 off;
    
    if (dex == NULL || out == NULL || field_idx >= dex->field_ids_size) {
        return 0;
    }
    
    off = dex->field_ids_off + field_idx * 8;
    
    if (!dex_bounds_ok (dex, off, 8)) {
        return 0;
    }
    
    out->class_idx = dex_read_u2 (dex, off + 0);
    out->type_idx = dex_read_u2 (dex, off + 2);
    out->name_idx = dex_read_u4 (dex, off + 4);
    
    return 1;

}

static int dex_get_proto_id (const DexFile *dex, u4 proto_idx, DexProtoId *out) {

    u4 off;
    
    if (dex == NULL || out == NULL || proto_idx >= dex->proto_ids_size) {
        return 0;
    }
    
    off = dex->proto_ids_off + proto_idx * 12;
    
    if (!dex_bounds_ok (dex, off, 12)) {
        return 0;
    }
    
    out->shorty_idx = dex_read_u4 (dex, off + 0);
    out->return_type_idx = dex_read_u4 (dex, off + 4);
    out->parameters_off = dex_read_u4 (dex, off + 8);
    
    return 1;

}

int dex_get_class_def (const DexFile *dex, u4 class_def_idx, DexClassDef *out) {

    u4 off;
    
    if (dex == NULL || out == NULL || class_def_idx >= dex->class_defs_size) {
        return 0;
    }
    
    off = dex->class_defs_off + class_def_idx * 32;
    
    if (!dex_bounds_ok (dex, off, 32)) {
        return 0;
    }
    
    out->class_idx = dex_read_u4 (dex, off + 0);
    out->access_flags = dex_read_u4 (dex, off + 4);
    out->superclass_idx = dex_read_u4 (dex, off + 8);
    out->interfaces_off = dex_read_u4 (dex, off + 12);
    out->source_file_idx = dex_read_u4 (dex, off + 16);
    out->annotations_off = dex_read_u4 (dex, off + 20);
    out->class_data_off = dex_read_u4 (dex, off + 24);
    out->static_values_off = dex_read_u4 (dex, off + 28);
    
    return 1;

}

static int dex_skip_encoded_fields (const DexFile *dex, u4 *off_io, u4 count) {

    u4 tmp;
    u4 i;
    
    for (i = 0; i < count; i++) {
    
        if (!dex_read_uleb128 (dex, off_io, &tmp)) {
            return 0;
        }
        
        if (!dex_read_uleb128 (dex, off_io, &tmp)) {
            return 0;
        }
    
    }
    
    return 1;

}

int dex_find_method_code (const DexFile *dex, const char *class_desc, const char *method_name, u4 *out_method_idx, u4 *out_code_off) {

    u4 class_i;
    
    if (dex == NULL || class_desc == NULL || method_name == NULL || out_method_idx == NULL || out_code_off == NULL) {
        return 0;
    }
    
    for (class_i = 0; class_i < dex->class_defs_size; class_i++) {
    
        DexClassDef cls;
        const char *desc;
        
        u4 off;
        u4 static_fields_size;
        u4 instance_fields_size;
        u4 direct_methods_size;
        u4 virtual_methods_size;
        u4 i;
        u4 method_idx;
        u4 method_idx_diff;
        
        if (!dex_get_class_def (dex, class_i, &cls) || !dex_get_type_descriptor (dex, cls.class_idx, &desc)) {
            return 0;
        }
        
        if (strcmp (desc, class_desc) != 0 || cls.class_data_off == 0) {
            continue;
        }

        off = cls.class_data_off;
        
        if (!dex_read_uleb128 (dex, &off, &static_fields_size) || !dex_read_uleb128 (dex, &off, &instance_fields_size) || !dex_read_uleb128 (dex, &off, &direct_methods_size) || !dex_read_uleb128 (dex, &off, &virtual_methods_size)) {
            return 0;
        }
        
        if (!dex_skip_encoded_fields (dex, &off, static_fields_size) || !dex_skip_encoded_fields (dex, &off, instance_fields_size)) {
            return 0;
        }
        
        method_idx = 0;
        
        for (i = 0; i < direct_methods_size; i++) {
        
            u4 access_flags;
            u4 code_off;
            
            DexMethodId mid;
            const char *name;
            
            if (!dex_read_uleb128 (dex, &off, &method_idx_diff) || !dex_read_uleb128 (dex, &off, &access_flags) || !dex_read_uleb128 (dex, &off, &code_off)) {
                return 0;
            }
            
            method_idx += method_idx_diff;
            
            if (!dex_get_method_id (dex, method_idx, &mid) || !dex_get_string (dex, mid.name_idx, &name)) {
                return 0;
            }
            
            if (strcmp (name, method_name) == 0) {
            
                *out_method_idx = method_idx;
                *out_code_off = code_off;
                
                (void)access_flags;
                return 1;
            
            }
        
        }
        
        method_idx = 0;
        
        for (i = 0; i < virtual_methods_size; i++) {
        
            u4 access_flags;
            u4 code_off;
            
            DexMethodId mid;
            const char *name;
            
            if (!dex_read_uleb128 (dex, &off, &method_idx_diff) || !dex_read_uleb128 (dex, &off, &access_flags) || !dex_read_uleb128 (dex, &off, &code_off)) {
                return 0;
            }
            
            method_idx += method_idx_diff;
            
            if (!dex_get_method_id (dex, method_idx, &mid) || !dex_get_string (dex, mid.name_idx, &name)) {
                return 0;
            }
            
            if (strcmp (name, method_name) == 0) {
            
                *out_method_idx = method_idx;
                *out_code_off = code_off;
                
                (void)access_flags;
                return 1;
            
            }
        
        }
    
    }
    
    return 0;

}

void dex_list_methods (const DexFile *dex) {

    u4 class_i;
    
    if (dex == NULL) {
        return;
    }
    
    for (class_i = 0; class_i < dex->class_defs_size; class_i++) {
    
        DexClassDef cls;
        const char *desc;
        
        u4 off;
        u4 static_fields_size;
        u4 instance_fields_size;
        u4 direct_methods_size;
        u4 virtual_methods_size;
        u4 i;
        u4 method_idx;
        u4 method_idx_diff;
        
        if (!dex_get_class_def (dex, class_i, &cls) || !dex_get_type_descriptor (dex, cls.class_idx, &desc) || cls.class_data_off == 0) {
            continue;
        }
        
        off = cls.class_data_off;
        
        if (!dex_read_uleb128 (dex, &off, &static_fields_size) || !dex_read_uleb128 (dex, &off, &instance_fields_size) || !dex_read_uleb128 (dex, &off, &direct_methods_size) || !dex_read_uleb128 (dex, &off, &virtual_methods_size)) {
            continue;
        }
        
        if (!dex_skip_encoded_fields (dex, &off, static_fields_size) || !dex_skip_encoded_fields (dex, &off, instance_fields_size)) {
            continue;
        }
        
        method_idx = 0;
        
        for (i = 0; i < direct_methods_size; i++) {
        
            u4 access_flags;
            u4 code_off;
            
            DexMethodId mid;
            const char *name;
            
            if (!dex_read_uleb128 (dex, &off, &method_idx_diff) || !dex_read_uleb128 (dex, &off, &access_flags) || !dex_read_uleb128 (dex, &off, &code_off)) {
                break;
            }
            
            method_idx += method_idx_diff;
            
            if (!dex_get_method_id (dex, method_idx, &mid) || !dex_get_string (dex, mid.name_idx, &name)) {
                continue;
            }
            
            printf ("%s %s", desc, name);
            
            if (code_off != 0) {
                printf ("  [code_off=%u]", code_off);
            } else {
                printf ("  [abstract/native]");
            }
            
            printf ("\n");
            (void)access_flags;
        
        }
        
        method_idx = 0;
        
        for (i = 0; i < virtual_methods_size; i++) {
        
            u4 access_flags;
            u4 code_off;
            
            DexMethodId mid;
            const char *name;
            
            if (!dex_read_uleb128 (dex, &off, &method_idx_diff) || !dex_read_uleb128 (dex, &off, &access_flags) || !dex_read_uleb128 (dex, &off, &code_off)) {
                break;
            }
            
            method_idx += method_idx_diff;
            
            if (!dex_get_method_id (dex, method_idx, &mid) || !dex_get_string (dex, mid.name_idx, &name)) {
                continue;
            }
            
            printf ("%s %s", desc, name);
            
            if (code_off != 0) {
                printf ("  [code_off=%u]", code_off);
            } else {
                printf ("  [abstract/native]");
            }
            
            printf ("\n");
            (void)access_flags;
        
        }
    
    }

}

int dex_get_code_off_by_method_idx (const DexFile *dex, u4 wanted_method_idx, u4 *out_code_off) {

    u4 class_i;
    
    if (dex == NULL || out_code_off == NULL) {
        return 0;
    }
    
    for (class_i = 0; class_i < dex->class_defs_size; class_i++) {
    
        DexClassDef cls;
        
        u4 off;
        u4 static_fields_size;
        u4 instance_fields_size;
        u4 direct_methods_size;
        u4 virtual_methods_size;
        u4 i;
        u4 method_idx;
        u4 method_idx_diff;
        
        if (!dex_get_class_def (dex, class_i, &cls) || cls.class_data_off == 0) {
            continue;
        }
        
        off = cls.class_data_off;
        
        if (!dex_read_uleb128 (dex, &off, &static_fields_size) || !dex_read_uleb128 (dex, &off, &instance_fields_size) || !dex_read_uleb128 (dex, &off, &direct_methods_size) || !dex_read_uleb128 (dex, &off, &virtual_methods_size)) {
            return 0;
        }
        
        if (!dex_skip_encoded_fields (dex, &off, static_fields_size) || !dex_skip_encoded_fields (dex, &off, instance_fields_size)) {
            return 0;
        }
        
        method_idx = 0;
        
        for (i = 0; i < direct_methods_size; i++) {
        
            u4 access_flags;
            u4 code_off;
            
            if (!dex_read_uleb128 (dex, &off, &method_idx_diff) || !dex_read_uleb128 (dex, &off, &access_flags) || !dex_read_uleb128 (dex, &off, &code_off)) {
                return 0;
            }
            
            method_idx += method_idx_diff;
            
            if (method_idx == wanted_method_idx) {
            
                *out_code_off = code_off;
                
                (void)access_flags;
                return 1;
            
            }
        
        }
        
        method_idx = 0;
        
        for (i = 0; i < virtual_methods_size; i++) {
        
            u4 access_flags;
            u4 code_off;
            
            if (!dex_read_uleb128 (dex, &off, &method_idx_diff) || !dex_read_uleb128 (dex, &off, &access_flags) || !dex_read_uleb128 (dex, &off, &code_off)) {
                return 0;
            }
            
            method_idx += method_idx_diff;
            
            if (method_idx == wanted_method_idx) {
            
                *out_code_off = code_off;
                
                (void)access_flags;
                return 1;
            
            }
        
        }
    
    }
    
    return 0;

}

int dex_load_file (const char *path, DexFile *dex) {

    FILE *fp;
    long file_len;
    
    u1 *data;
    size_t nread;
    
    if (path == NULL || dex == NULL) {
        return 0;
    }
    
    memset (dex, 0, sizeof(*dex));
    fp = fopen(path, "rb");
    
    if (fp == NULL) {
    
        fprintf (stderr, "could not open dex: %s\n", path);
        return 0;
    
    }
    
    if (fseek (fp, 0, SEEK_END) != 0) {
    
        fclose (fp);
        return 0;
    
    }
    
    file_len = ftell (fp);
    
    if (file_len < 0) {
    
        fclose (fp);
        return 0;
    
    }
    
    if (fseek (fp, 0, SEEK_SET) != 0) {
    
        fclose (fp);
        return 0;
    
    }
    
    data = (u1 *) malloc ((size_t) file_len);
    
    if (data == NULL) {
    
        fclose (fp);
        return 0;
    
    }
    
    nread = fread (data, 1, (size_t) file_len, fp);
    fclose (fp);
    
    if (nread != (size_t) file_len) {
    
        free (data);
        return 0;
    
    }
    
    dex->data = data;
    dex->size = (u4) file_len;
    
    if (!dex_bounds_ok (dex, 0, 112) || !dex_is_magic (dex->data)) {
    
        fprintf (stderr, "not a valid dex file\n");
        free (data);
        
        memset (dex, 0, sizeof(*dex));
        return 0;
    
    }
    
    if (dex_read_u4 (dex, 40) != 0x12345678U) {
    
        fprintf (stderr, "unsupported endian tag\n");
        free (data);
        
        memset (dex, 0, sizeof(*dex));
        return 0;
    
    }
    
    dex->string_ids_size = dex_read_u4 (dex, 56);
    dex->string_ids_off  = dex_read_u4 (dex, 60);
    dex->type_ids_size   = dex_read_u4 (dex, 64);
    dex->type_ids_off    = dex_read_u4 (dex, 68);
    dex->proto_ids_size  = dex_read_u4 (dex, 72);
    dex->proto_ids_off   = dex_read_u4 (dex, 76);
    dex->field_ids_size  = dex_read_u4 (dex, 80);
    dex->field_ids_off   = dex_read_u4 (dex, 84);
    dex->method_ids_size = dex_read_u4 (dex, 88);
    dex->method_ids_off  = dex_read_u4 (dex, 92);
    dex->class_defs_size = dex_read_u4 (dex, 96);
    dex->class_defs_off  = dex_read_u4 (dex, 100);
    
    return 1;

}

void dex_free (DexFile *dex) {

    if (dex != NULL) {
    
        free (dex->data);
        memset (dex, 0, sizeof(*dex));
    
    }

}

int dex_proto_return_desc (const DexFile *dex, u2 proto_idx, const char **out) {

    u4 return_type_idx;
    u4 off;
    
    if (dex == NULL || out == NULL || proto_idx >= dex->proto_ids_size) {
        return 0;
    }
    
    off = dex->proto_ids_off + (u4)proto_idx * 12U;
    
    if (!dex_bounds_ok (dex, off, 12)) {
        return 0;
    }
    
    return_type_idx = dex_read_u4 (dex, off + 4);
    return dex_get_type_descriptor (dex, return_type_idx, out);

}

int proto_param_count (const DexFile *dex, u2 proto_idx);
int proto_param_desc (const DexFile *dex, u2 proto_idx, int index, const char **out);

int build_param_sig_text (const DexFile *dex, u2 proto_idx, char *out, size_t out_sz) {

    int count;
    int i;
    
    size_t used;

    if (out == NULL || out_sz == 0) {
        return 0;
    }
    
    out[0] = '\0';
    used = 0;
    
    if (used + 1 >= out_sz) {
        return 0;
    }
    
    out[used++] = '(';
    out[used] = '\0';
    
    count = proto_param_count (dex, proto_idx);
    
    for (i = 0; i < count; i++) {
    
        const char *pdesc;
        char jname[128];
        
        size_t n;
        
        if (!proto_param_desc (dex, proto_idx, i, &pdesc)) {
            return 0;
        }
        
        desc_to_java_name (pdesc, jname, sizeof(jname));
        n = strlen (jname);
        
        if (used + n + 2 >= out_sz) {
            return 0;
        }
        
        memcpy (out + used, jname, n);
        used += n;
        
        if (i + 1 < count) {
            out[used++] = ',';
        }
        
        out[used] = '\0';
    
    }
    
    if (used + 2 >= out_sz) {
        return 0;
    }
    
    out[used++] = ')';
    out[used] = '\0';
    
    return 1;

}

int dex_method_name_parts (const DexFile *dex, u4 method_idx, const char **class_desc, const char **method_name, u2 *proto_idx) {

    DexMethodId mid;
    
    if (!dex_get_method_id (dex, method_idx, &mid) || !dex_get_type_descriptor (dex, mid.class_idx, class_desc) || !dex_get_string (dex, mid.name_idx, method_name)) {
        return 0;
    }
    
    if (proto_idx != NULL) {
        *proto_idx = mid.proto_idx;
    }
    
    return 1;

}

int dex_field_name_parts (const DexFile *dex, u4 field_idx, const char **class_desc, const char **field_name) {

    DexFieldId fid;
    
    if (!dex_get_field_id(dex, field_idx, &fid) || !dex_get_type_descriptor (dex, fid.class_idx, class_desc) || !dex_get_string (dex, fid.name_idx, field_name)) {
        return 0;
    }
    
    return 1;

}

int proto_param_count (const DexFile *dex, u2 proto_idx) {

    DexProtoId proto;
    u4 off;
    
    if (!dex_get_proto_id (dex, proto_idx, &proto)) {
        return -1;
    }
    
    if (proto.parameters_off == 0) {
        return 0;
    }
    
    off = proto.parameters_off;
    
    if (!dex_bounds_ok (dex, off, 4)) {
        return -1;
    }
    
    return (int) dex_read_u4 (dex, off);

}

int proto_param_desc (const DexFile *dex, u2 proto_idx, int index, const char **out) {

    DexProtoId proto;
    
    u4 off;
    u4 size;
    u2 type_idx;
    
    if (!dex_get_proto_id (dex, proto_idx, &proto) || out == NULL) {
        return 0;
    }
    
    if (proto.parameters_off == 0) {
        return 0;
    }
    
    off = proto.parameters_off;
    
    if (!dex_bounds_ok (dex, off, 4)) {
        return 0;
    }
    
    size = dex_read_u4 (dex, off);
    
    if (index < 0 || (u4) index >= size) {
        return 0;
    }
    
    type_idx = dex_read_u2 (dex, off + 4 + index * 2);
    return dex_get_type_descriptor (dex, type_idx, out);

}

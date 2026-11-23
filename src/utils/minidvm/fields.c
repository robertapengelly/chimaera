/******************************************************************************
 * @file            fields.c
 *****************************************************************************/
#include "mini_dalvik_vm.h"

static StaticSlot *g_static_fields = NULL;

int set_static_field_value (const DexFile *dex, u4 field_idx, Value v) {

    StaticSlot *p;
    
    if (dex == NULL) {
        return 0;
    }
    
    for (p = g_static_fields; p != NULL; p = p->next) {
    
        if (p->dex == dex && p->field_idx == field_idx) {
        
            p->value = v;
            return 1;
        
        }
    
    }
    
    p = (StaticSlot *) calloc(1, sizeof (*p));
    
    if (p == NULL) {
        return 0;
    }
    
    p->dex = dex;
    p->field_idx = field_idx;
    p->value = v;
    p->next = g_static_fields;
    
    g_static_fields = p;
    return 1;

}

static int get_static_field_value (const DexFile *dex, u4 field_idx, Value *out) {

    StaticSlot *p;
    
    if (dex == NULL || out == NULL) {
        return 0;
    }
    
    for (p = g_static_fields; p != NULL; p = p->next) {
    
        if (p->dex == dex && p->field_idx == field_idx) {
        
            *out = p->value;
            return 1;
        
        }
    
    }
    
    return 0;

}

static int dex_read_encoded_value (const DexFile *dex, u4 *off_io, Value *out) {

    u1 header;
    u4 value_type;
    u4 value_arg;
    u4 count;
    u4 i;
    
    unsigned long long bits;
    unsigned long long sign_bit;
    
    const char *text;
    
    float f;
    double d;
    
    if (dex == NULL || off_io == NULL || out == NULL || !dex_bounds_ok (dex, *off_io, 1)) {
        return 0;
    }
    
    header = dex->data[(*off_io)++];
    
    value_type = header & 0x1fU;
    value_arg = header >> 5;
    
    count = value_arg + 1;
    bits = 0;
    
    switch (value_type) {
    
        case 0x00: /* VALUE_BYTE */     case 0x02: /* VALUE_SHORT */        case 0x03: /* VALUE_CHAR */     case 0x04: /* VALUE_INT */      case 0x06: /* VALUE_LONG */
        
            if (count > 8 || !dex_bounds_ok (dex, *off_io, count)) {
                return 0;
            }
            
            for (i = 0; i < count; i++) {
                bits |= ((unsigned long long) dex->data[(*off_io)++]) << (i * 8);
            }
            
            if (value_type != 0x03 && count < 8) {
            
                sign_bit = 1ULL << (count * 8 - 1);
                
                if ((bits & sign_bit) != 0) {
                    bits |= ~0ULL << (count * 8);
                }
            
            }
            
            if (value_type == 0x06) {
                *out = value_wide (bits);
            } else {
                *out = value_int ((long) (long long) bits);
            }
            
            return 1;
        
        case 0x10:                      /* VALUE_FLOAT */
        
            if (count > 4 || !dex_bounds_ok (dex, *off_io, count)) {
                return 0;
            }
            
            for (i = 0; i < count; i++) {
                bits |= ((unsigned long long) dex->data[(*off_io)++]) << (i * 8);
            }
            
            bits <<= (4 - count) * 8;
            
            {
                u4 fbits = (u4) bits;
                
                memcpy (&f, &fbits, sizeof (f));
                *out = value_int ((long) (s4) fbits);
            
            }
            
            return 1;
        
        case 0x11:                      /* VALUE_DOUBLE */
        
            if (count > 8 || !dex_bounds_ok (dex, *off_io, count)) {
                return 0;
            }
            
            for (i = 0; i < count; i++) {
                bits |= ((unsigned long long) dex->data[(*off_io)++]) << (i * 8);
            }
            
            bits <<= (8 - count) * 8;
            
            memcpy (&d, &bits, sizeof (d));
            *out = value_double (d);
            
            return 1;
        
        case 0x17:                      /* VALUE_STRING */
        
            if (count > 4 || !dex_bounds_ok (dex, *off_io, count)) {
                return 0;
            }
            
            for (i = 0; i < count; i++) {
                bits |= ((unsigned long long) dex->data[(*off_io)++]) << (i * 8);
            }
            
            if (!dex_get_string (dex, (u4) bits, &text)) {
                return 0;
            }
            
            *out = value_obj (obj_new_string (text));
            return out->obj != NULL;
        
        case 0x1e:                      /* VALUE_NULL */
        
            if (value_arg != 0) {
                return 0;
            }
            
            *out = value_obj (NULL);
            return 1;
        
        case 0x1f:                      /* VALUE_BOOLEAN */
        
            if (value_arg > 1) {
                return 0;
            }
            
            *out = value_int ((long) value_arg);
            return 1;
    
    }
    
    return 0;

}

static Value default_static_value_for_field (const DexFile *dex, u4 field_idx) {

    DexFieldId fid;
    const char *desc;
    
    if (!dex_get_field_id (dex, field_idx, &fid) || !dex_get_type_descriptor (dex, fid.type_idx, &desc)) {
        return value_int (0);
    }
    
    if (strcmp(desc, "J") == 0) {
        return value_wide (0);
    }
    
    if (strcmp(desc, "D") == 0) {
        return value_double (0.0);
    }
    
    if (strcmp(desc, "F") == 0) {
        return value_int (0);
    }
    
    if (desc[0] == 'L' || desc[0] == '[') {
        return value_obj (NULL);
    }
    
    return value_int (0);

}

int initialise_static_fields (const DexFile *dex) {

    u4 class_i;
    
    if (dex == NULL) {
        return 0;
    }
    
    for (class_i = 0; class_i < dex->class_defs_size; class_i++) {
    
        DexClassDef cls;
        
        u4 off;
        u4 static_count;
        u4 instance_count;
        u4 direct_count;
        u4 virtual_count;
        u4 field_idx;
        u4 field_diff;
        u4 access_flags;
        u4 i;
        u4 values_off;
        u4 values_count;
        
        if (!dex_get_class_def (dex, class_i, &cls)) {
            return 0;
        }
        
        if (cls.class_data_off == 0) {
            continue;
        }
        
        off = cls.class_data_off;
        
        if (!dex_read_uleb128 (dex, &off, &static_count) || !dex_read_uleb128 (dex, &off, &instance_count) || !dex_read_uleb128 (dex, &off, &direct_count) || !dex_read_uleb128 (dex, &off, &virtual_count)) {
            return 0;
        }
        
        values_off = cls.static_values_off;
        values_count = 0;
        
        if (values_off != 0) {
        
            if (!dex_read_uleb128 (dex, &values_off, &values_count) || values_count > static_count) {
                return 0;
            }
        
        }
        
        field_idx = 0;
        
        for (i = 0; i < static_count; i++) {
        
            Value value;
            
            if (!dex_read_uleb128 (dex, &off, &field_diff) || !dex_read_uleb128 (dex, &off, &access_flags)) {
                return 0;
            }
            
            field_idx += field_diff;
            value = default_static_value_for_field (dex, field_idx);
            
            if (i < values_count && !dex_read_encoded_value (dex, &values_off, &value)) {
                return 0;
            }
            
            if (!set_static_field_value (dex, field_idx, value)) {
                return 0;
            }
            
            (void) access_flags;
        
        }
        
        (void) instance_count;
        (void) direct_count;
        (void) virtual_count;
    
    }
    
    return 1;

}

int get_static_object_field (const DexFile *dex, u4 field_idx, Value *out) {

    const char *cls;
    const char *name;
    
    const ApiField *api_f;
    DexFieldId fid;
    
    const char *type_desc;
    
    char cls_java[128];
    char type_java[128];
    
    if (out == NULL) {
        return 0;
    }
    
    if (get_static_field_value (dex, field_idx, out)) {
        return 1;
    }
    
    if (!dex_field_name_parts (dex, field_idx, &cls, &name)) {
        return 0;
    }
    
    if (!dex_get_field_id (dex, field_idx, &fid)) {
        return 0;
    }
    
    if (!dex_get_type_descriptor (dex, fid.type_idx, &type_desc)) {
        return 0;
    }
    
    desc_to_java_name (cls, cls_java, sizeof (cls_java));
    desc_to_java_name (type_desc, type_java, sizeof (type_java));
    
    if (!g_api.loaded) {
        return 0;
    }
    
    api_f = api_find_field (&g_api, cls_java, name);
    
    if (api_f == NULL) {
        return 0;
    }
    
    switch (api_f->builtin_id) {
    
        case BUILTIN_FIELD_SYSTEM_OUT:
        
            *out = value_obj (obj_new_print_stream ());
            return out->obj != NULL;
    
    }
    
    return 0;

}

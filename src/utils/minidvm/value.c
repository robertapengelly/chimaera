/******************************************************************************
 * @file            value.c
 *****************************************************************************/
#include "mini_dalvik_vm.h"

s4 sx4 (u4 v) {

    if (v & 0x8U) {
        return (s4) (v | ~0x0fU);
    }
    
    return (s4) v;

}

s4 sx8 (u4 v) {
    
    if (v & 0x80U) {
        return (s4) (v | ~0xffU);
    }
    
    return (s4) v;

}

s4 sx16 (u4 v) {

    if (v & 0x8000U) {
        return (s4) (v | ~0xffffU);
    }
    
    return (s4) v;

}

Value value_int (long v) {

    Value x;
    
    x.kind = VAL_INT;
    x.i = v;
    
    x.wide = (unsigned long long) (long long) v;
    
    x.d = 0.0;
    x.obj = NULL;
    
    return x;

}

Value value_wide (unsigned long long v) {

    Value x;
    
    x.kind = VAL_WIDE;
    x.i = (long) (long long) v;
    
    x.wide = v;
    
    x.d = 0.0;
    x.obj = NULL;
    
    return x;

}

Value value_double (double v) {

    Value x;
    
    x.kind = VAL_DOUBLE;
    x.i = 0;
    
    x.wide = 0;
    
    x.d = v;
    x.obj = NULL;
    
    return x;

}

Value value_obj (Obj *obj) {

    Value x;
    
    x.kind = VAL_OBJ;
    x.i = 0;
    
    x.wide = 0;
    
    x.d = 0.0;
    x.obj = obj;
    
    return x;

}

Obj *obj_new_string (const char *s) {

    Obj *o;
    size_t len;
    
    if (s == NULL) {
        s = "";
    }
    
    o = (Obj *) calloc (1, sizeof (*o));
    
    if (o == NULL) {
        return NULL;
    }
    
    o->type = OBJ_STRING;
    len = strlen(s);
    
    o->u.string.text = (char *)malloc(len + 1);
    
    if (o->u.string.text == NULL) {
    
        free (o);
        return NULL;
    }
    
    memcpy (o->u.string.text, s, len + 1);
    return o;

}

Obj *obj_new_print_stream (void) {

    Obj *o = (Obj *) calloc (1, sizeof (*o));
    
    if (o != NULL) {
        o->type = OBJ_PRINT_STREAM;
    }
    
    return o;

}

static Obj *obj_new_string_builder (void) {

    Obj *o = (Obj *) calloc (1, sizeof (*o));
    
    if (o != NULL) {
    
        o->type = OBJ_STRING_BUILDER;
        o->u.sb.text[0] = '\0';
    
    }
    
    return o;

}

static Obj *obj_new_generic (const char *class_desc) {

    Obj *o = (Obj *) calloc (1, sizeof (*o));
    
    if (o != NULL) {
    
        o->type = OBJ_GENERIC;
        o->class_desc = class_desc;
    
    }
    
    return o;

}

Obj *obj_new_class (const char *class_desc) {

    Obj *o = (Obj *) calloc (1, sizeof (*o));
    
    if (o != NULL) {
    
        o->type = OBJ_CLASS;
        o->class_desc = class_desc;
    
    }
    
    return o;

}

Obj *obj_new_array (int len) {

    Obj *o = (Obj *) calloc (1, sizeof (*o));
    
    if (o == NULL) {
        return NULL;
    }
    
    o->type = OBJ_ARRAY;
    o->array_len = len;
    
    if (len > 0) {
    
        o->array_elems = (Value *) calloc ((size_t) len, sizeof (Value));
        
        if (o->array_elems == NULL) {
        
            free (o);
            return NULL;
        
        }
    
    }
    
    return o;

}

int obj_set_field (Obj *o, u4 field_idx, Value v) {

    FieldSlot *p;
    
    if (o == NULL) {
        return 0;
    }
    
    for (p = o->fields; p != NULL; p = p->next) {
    
        if (p->field_idx == field_idx) {
        
            p->value = v;
            return 1;
        
        }
    
    }
    
    p = (FieldSlot *) calloc (1, sizeof (*p));
    
    if (p == NULL) {
        return 0;
    }
    
    p->field_idx = field_idx;
    p->value = v;
    p->next = o->fields;
    
    o->fields = p;
    return 1;

}

int obj_get_field (Obj *o, u4 field_idx, Value *out) {

    FieldSlot *p;
    
    if (o == NULL || out == NULL) {
        return 0;
    }
    
    for (p = o->fields; p != NULL; p = p->next) {
    
        if (p->field_idx == field_idx) {
        
            *out = p->value;
            return 1;
        
        }
    
    }
    
    return 0;

}

int array_set (Obj *o, int index, Value v) {

    if (o == NULL || o->type != OBJ_ARRAY || index < 0 || index >= o->array_len) {
        return 0;
    }
    
    o->array_elems[index] = v;
    return 1;

}

int array_get (Obj *o, int index, Value *out) {

    if (o == NULL || o->type != OBJ_ARRAY || out == NULL || index < 0 || index >= o->array_len) {
        return 0;
    }
    
    *out = o->array_elems[index];
    return 1;

}

void sb_append_text (Obj *o, const char *s) {

    size_t cur;
    size_t add;
    size_t room;
    
    if (o == NULL || o->type != OBJ_STRING_BUILDER || s == NULL) {
        return;
    }
    
    cur = strlen(o->u.sb.text);
    add = strlen(s);
    
    if (cur >= MAX_SB_TEXT - 1) {
        return;
    }
    
    room = (MAX_SB_TEXT - 1) - cur;
    
    if (add > room) {
        add = room;
    }
    
    memcpy (o->u.sb.text + cur, s, add);
    o->u.sb.text[cur + add] = '\0';

}

void sb_append_long (Obj *o, long v) {

    char buf[64];
    sprintf (buf, "%ld", v);
    
    sb_append_text (o, buf);

}

void sb_append_double (Obj *o, double d) {

    char buf[128];
    sprintf (buf, "%g", d);
    
    sb_append_text (o, buf);

}

Obj *obj_new_for_desc (const char *desc) {

    char cls_java[128];
    
    if (desc == NULL) {
        return NULL;
    }
    
    desc_to_java_name (desc, cls_java, sizeof (cls_java));
    
    if (g_api.loaded) {
    
        if (strcmp (cls_java, "java.lang.StringBuilder") == 0) {
            return obj_new_string_builder ();
        }
        if (strcmp (cls_java, "java.io.PrintStream") == 0) {
            return obj_new_print_stream ();
        }
    
    }

    return obj_new_generic (desc);

}

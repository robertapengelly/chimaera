#ifndef MINI_DALVIK_VM_H
#define MINI_DALVIK_VM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

typedef unsigned char  u1;
typedef unsigned short u2;
typedef unsigned int   u4;
typedef signed int     s4;

#define DVM_MAX_REGS 256
#define MAX_SB_TEXT 1024

enum {
    VAL_UNINIT = 0,
    VAL_INT,
    VAL_WIDE,
    VAL_DOUBLE,
    VAL_OBJ
};

enum {
    OBJ_UNKNOWN = 0,
    OBJ_PRINT_STREAM,
    OBJ_STRING,
    OBJ_STRING_BUILDER,
    OBJ_GENERIC,
    OBJ_CLASS,
    OBJ_ARRAY
};

typedef struct FieldSlot FieldSlot;
typedef struct StaticSlot StaticSlot;

typedef struct Obj Obj;
typedef struct Value Value;
typedef struct DexFile DexFile;

typedef struct {
    u2 class_idx;
    u2 proto_idx;
    u4 name_idx;
} DexMethodId;

typedef struct {
    u2 class_idx;
    u2 type_idx;
    u4 name_idx;
} DexFieldId;

typedef struct {
    u4 class_idx;
    u4 access_flags;
    u4 superclass_idx;
    u4 interfaces_off;
    u4 source_file_idx;
    u4 annotations_off;
    u4 class_data_off;
    u4 static_values_off;
} DexClassDef;

typedef struct {
    u4 shorty_idx;
    u4 return_type_idx;
    u4 parameters_off;
} DexProtoId;

struct DexFile {
    u1 *data;
    u4 size;
    u4 string_ids_size;
    u4 string_ids_off;
    u4 type_ids_size;
    u4 type_ids_off;
    u4 proto_ids_size;
    u4 proto_ids_off;
    u4 field_ids_size;
    u4 field_ids_off;
    u4 method_ids_size;
    u4 method_ids_off;
    u4 class_defs_size;
    u4 class_defs_off;
};

struct Obj {
    int type;
    const char *class_desc;
    FieldSlot *fields;
    int array_len;
    Value *array_elems;
    union {
        struct {
            char *text;
        } string;
        struct {
            char text[MAX_SB_TEXT];
        } sb;
    } u;
};

struct Value {
    int kind;
    long i;
    unsigned long long wide;
    double d;
    Obj *obj;
};

struct FieldSlot {
    u4 field_idx;
    Value value;
    FieldSlot *next;
};

struct StaticSlot {
    const DexFile *dex;
    u4 field_idx;
    Value value;
    StaticSlot *next;
};

typedef struct ApiMethod ApiMethod;
typedef struct ApiField ApiField;
typedef struct ApiDB ApiDB;

enum {
    BUILTIN_NONE = 0,

    BUILTIN_MATH_RANDOM,

    BUILTIN_SB_INIT,
    BUILTIN_SB_APPEND_STRING,
    BUILTIN_SB_APPEND_INT,
    BUILTIN_SB_APPEND_LONG,
    BUILTIN_SB_APPEND_CHAR,
    BUILTIN_SB_APPEND_FLOAT,
    BUILTIN_SB_APPEND_DOUBLE,
    BUILTIN_SB_TO_STRING,

    BUILTIN_PRINTLN_STRING,
    BUILTIN_PRINTLN_INT,
    BUILTIN_PRINTLN_DOUBLE,

    BUILTIN_LOG_I,
    BUILTIN_LOG_D,

    BUILTIN_FIELD_SYSTEM_OUT
};

struct ApiMethod {
    char class_name[128];
    char method_name[64];
    char params[256];
    char ret_type[128];
    int builtin_id;
    ApiMethod *next;
};

struct ApiField {
    char class_name[128];
    char field_name[64];
    char field_type[128];
    int builtin_id;
    ApiField *next;
};

struct ApiDB {
    ApiMethod *methods;
    ApiField *fields;
    int loaded;
};

#define MAX_LOADED_DEX 10


extern ApiDB g_api;
extern Obj *g_pending_exception;
extern DexFile g_loaded_dexes[MAX_LOADED_DEX];
extern int g_dex_count;
extern char g_base_path[256];

/* api.c */
void api_init(ApiDB *db);
void api_free(ApiDB *db);
int api_builtin_method_id(const char *cls, const char *name,
                                 const char *params, const char *ret);
void api_add_core_builtins(ApiDB *db);
int api_load_file(ApiDB *db, const char *path);
const ApiMethod *api_find_method(const ApiDB *db, const char *cls, const char *name, const char *params);
const ApiField *api_find_field(const ApiDB *db, const char *cls, const char *name);
void desc_to_java_name(const char *desc, char *out, size_t out_sz);

/* dex.c */
u2 dex_read_u2(const DexFile *dex, u4 off);
u4 dex_read_u4(const DexFile *dex, u4 off);
int dex_bounds_ok(const DexFile *dex, u4 off, u4 size);
int dex_read_uleb128(const DexFile *dex, u4 *off_io, u4 *out);
int dex_find_exception_handler(const DexFile *dex,
                                      u4 code_off,
                                      u4 throw_pc,
                                      const char *exception_desc,
                                      u4 *handler_pc);
int dex_get_string(const DexFile *dex, u4 string_idx, const char **out);
int dex_get_type_descriptor(const DexFile *dex, u4 type_idx, const char **out);
int dex_get_field_id(const DexFile *dex, u4 field_idx, DexFieldId *out);
int dex_get_class_def(const DexFile *dex, u4 class_def_idx, DexClassDef *out);
int dex_find_method_code(const DexFile *dex,
                                const char *class_desc,
                                const char *method_name,
                                u4 *out_method_idx,
                                u4 *out_code_off);
void dex_list_methods(const DexFile *dex);
int dex_get_code_off_by_method_idx(const DexFile *dex, u4 wanted_method_idx, u4 *out_code_off);
int dex_load_file(const char *path, DexFile *dex);
void dex_free(DexFile *dex);
int dex_proto_return_desc(const DexFile *dex, u2 proto_idx, const char **out);
int build_param_sig_text(const DexFile *dex, u2 proto_idx, char *out, size_t out_sz);
int dex_method_name_parts(const DexFile *dex, u4 method_idx,
                                 const char **class_desc,
                                 const char **method_name,
                                 u2 *proto_idx);
int dex_field_name_parts(const DexFile *dex, u4 field_idx,
                                const char **class_desc,
                                const char **field_name);
int proto_param_count(const DexFile *dex, u2 proto_idx);

/* value.c */
s4 sx4(u4 v);
s4 sx8(u4 v);
s4 sx16(u4 v);
Value value_int(long v);
Value value_wide(unsigned long long v);
Value value_double(double v);
Value value_obj(Obj *obj);
Obj *obj_new_string(const char *s);
Obj *obj_new_print_stream(void);
Obj *obj_new_class(const char *class_desc);
Obj *obj_new_array(int len);
int obj_set_field(Obj *o, u4 field_idx, Value v);
int obj_get_field(Obj *o, u4 field_idx, Value *out);
int array_set(Obj *o, int index, Value v);
int array_get(Obj *o, int index, Value *out);
void sb_append_text(Obj *o, const char *s);
void sb_append_long(Obj *o, long v);
void sb_append_double(Obj *o, double d);
Obj *obj_new_for_desc(const char *desc);

/* builtin.c */
int call_builtin(const DexFile *dex, u4 method_idx, Value *args, int argc, Value *out);

/* fields.c */
int set_static_field_value(const DexFile *dex, u4 field_idx, Value v);
int initialise_static_fields(const DexFile *dex);
int get_static_object_field(const DexFile *dex, u4 field_idx, Value *out);

/* loader.c */
void set_base_path(const char *path);
const DexFile* find_dex_for_class(const char* class_desc);
int try_dynamic_load(const char *class_desc);

/* execute.c */
int exec_method_by_code(const DexFile *dex,
                               const char *class_desc,
                               const char *method_name,
                               u4 method_idx,
                               u4 code_off,
                               int trace,
                               Value *ret);
int exec_method(const DexFile *dex,
                       const char *class_desc,
                       const char *method_name,
                       int trace,
                       Value *ret);

#endif

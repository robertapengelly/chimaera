/******************************************************************************
 * @file            compile.c
 *****************************************************************************/
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "j2d.h"
#include    "int64.h"
#include    "lib.h"
#include    "report.h"
#include    "token.h"

struct concat_field_ref {

    char *name;
    
    const char *filename;
    unsigned long line;
    
    const char *start;
    const char *caret;

};

struct statement_info {

    int kind;
    
    char *target;
    char *method;
    
    int arg_kind;
    char *arg_text;
    
    const char *filename;
    unsigned long line;
    
    const char *start;
    const char *caret;

    struct concat_field_ref *concat_refs;
    unsigned long nb_concat_refs;
    
    unsigned long arg_param_index;
    
    unsigned long branch_offset;
    unsigned long code_offset;
    
    unsigned long jump_target;

};

struct field_mul_expr {

    char *field;
    long constant;
    
    char *factors;
    unsigned long factors_len;

    char *literal_text, type;
    
    int has_field;
    int is_constant;

};

#define     STMT_CALL                   1
#define     STMT_RETURN                 2
#define     STMT_LOCAL                  3
#define     STMT_THROW                  4
#define     STMT_GOTO                   5
#define     STMT_MOVE_EXCEPTION         6
#define     STMT_LABEL                  7
#define     STMT_IF_FALSE               8
#define     STMT_IF_EQ_CONST            9
#define     STMT_ARRAY_PUT              10

#define     ARG_STRING                  1
#define     ARG_PARAM                   2
#define     ARG_CALL                    3
#define     ARG_INT                     4
#define     ARG_STRING_CONCAT           5
#define     ARG_ARITHMETIC              6
#define     ARG_NEW                     7
#define     ARG_NONE                    8
#define     ARG_MULTI                   9
#define     ARG_NEW_ARRAY               10
#define     ARG_ARRAY_GET               11
#define     ARG_ARRAY_LENGTH            12
#define     ARG_RESULT_CALL             13
#define     ARG_RESULT_MULTI            14
#define     ARG_THIS                    15
#define     ARG_INSTANCEOF              16
#define     ARG_CHECKCAST               17
#define     ARG_STATIC_GET              18

struct local_info {

    char *name;
    char *type;
    
    unsigned long scope_depth;
    int active;

};

struct try_info {

    unsigned long start_stmt;
    unsigned long end_stmt;
    unsigned long handler_stmt;
    
    char *type;

};

struct method_info {

    char *name, *java_name;
    
    char *params;
    char *shorty;
    
    char *return_type;
    unsigned int flags;
    
    char **throws_types;
    unsigned long nb_throws_types;
    
    char **param_names;
    unsigned long nb_param_names;
    
    unsigned long local_scope_depth;
    
    struct local_info *locals;
    unsigned long nb_locals;
    
    struct statement_info *statements;
    unsigned long nb_statements;
    
    struct try_info *tries;
    unsigned long nb_tries;
    unsigned long try_depth;

};

struct field_info {

    char *name;
    char *type;
    
    char *init_text;
    
    int is_constructor_initializer;
    int has_initializer;
    
    const char *init_filename;
    unsigned long init_line;
    
    const char *init_start;
    const char *init_caret;
    
    unsigned int flags;

};

struct import_info {

    char *name;
    
    int wildcard;
    int implicit;
    
    char *filename;
    unsigned long line;
    
    char *source_line;
    unsigned long caret_offset;

};

struct source_names {

    char *package_name;
    char *super_class;
    
    char *class_name;
    int is_interface;
    
    char **interfaces;
    unsigned long nb_interfaces;
    
    struct import_info *imports;
    unsigned long nb_imports;
    
    struct method_info *methods;
    unsigned long nb_methods;
    
    struct field_info *fields;
    unsigned long nb_fields;
    
    unsigned int class_flags;
    
    struct source_names *nested_classes;
    unsigned long nb_nested_classes;

};

static struct source_names *declaration_names;
static unsigned long nb_declaration_names;

static int validate_declarations_only;
static int collect_declarations_only;

#define     METHOD_PUBLIC               0x0001
#define     METHOD_PRIVATE              0x0002
#define     METHOD_PROTECTED            0x0004
#define     METHOD_STATIC               0x0008
#define     METHOD_FINAL                0x0010
#define     METHOD_SYNCHRONIZED         0x0020
#define     FIELD_VOLATILE              0x0040
#define     FIELD_TRANSIENT             0x0080
#define     METHOD_NATIVE               0x0100
#define     METHOD_ABSTRACT             0x0400
#define     METHOD_STRICTFP             0x0800

#define     METHOD_ACCESS_DENIED        0xFFFFFFFEUL
#define     METHOD_NOT_FOUND            0xFFFFFFFFUL

#define     CLASS_PUBLIC                0x0001
#define     CLASS_FINAL                 0x0010
#define     CLASS_INTERFACE             0x0200
#define     CLASS_ABSTRACT              0x0400
#define     CLASS_STRICTFP              0x0800

#define     DEX_FIELD_PUBLIC            0x0001
#define     DEX_FIELD_PRIVATE           0x0002
#define     DEX_FIELD_PROTECTED         0x0004
#define     DEX_FIELD_STATIC            0x0008
#define     DEX_FIELD_FINAL             0x0010
#define     DEX_FIELD_VOLATILE          0x0040
#define     DEX_FIELD_TRANSIENT         0x0080

#define     JAVA_OBJECT_TYPE            "Ljava/lang/Object;"
#define     JAVA_STRING_TYPE            "Ljava/lang/String;"

#define     JAVA_RUNTIME_EXCEPTION_TYPE "Ljava/lang/RuntimeException;"
#define     JAVA_ERROR_TYPE             "Ljava/lang/Error;"

static unsigned int field_access_flags (struct field_info *field) {

    unsigned int flags = 0;
    
    if (field->flags & METHOD_PUBLIC) {
        flags |= DEX_FIELD_PUBLIC;
    }
    
    if (field->flags & METHOD_PRIVATE) {
        flags |= DEX_FIELD_PRIVATE;
    }
    
    if (field->flags & METHOD_PROTECTED) {
        flags |= DEX_FIELD_PROTECTED;
    }
    
    if (field->flags & METHOD_STATIC) {
        flags |= DEX_FIELD_STATIC;
    }
    
    if (field->flags & METHOD_FINAL) {
        flags |= DEX_FIELD_FINAL;
    }
    
    if (field->flags & FIELD_VOLATILE) {
        flags |= DEX_FIELD_VOLATILE;
    }
    
    if (field->flags & FIELD_TRANSIENT) {
        flags |= DEX_FIELD_TRANSIENT;
    }
    
    return flags;

}

static unsigned int class_access_flags (struct source_names *names) {

    unsigned int flags = names->class_flags;
    
    if (names->is_interface) {
        flags |= CLASS_INTERFACE | CLASS_ABSTRACT;
    }
    
    return flags;

}

static char *dex_class_descriptor (struct source_names *names);

static void free_source_names (struct source_names *names) {

    unsigned long i;
    
    if (names->package_name) {
        free (names->package_name);
    }
    
    if (names->class_name) {
        free (names->class_name);
    }
    
    if (names->super_class) {
        free (names->super_class);
    }
    
    for (i = 0; i < names->nb_interfaces; i++) {
        free (names->interfaces[i]);
    }
    
    free (names->interfaces);
    
    for (i = 0; i < names->nb_imports; i++) {
    
        free (names->imports[i].name);
        free (names->imports[i].filename);
        free (names->imports[i].source_line);
    
    }
    
    free (names->imports);
    
    for (i = 0; i < names->nb_methods; i++) {
    
        free (names->methods[i].java_name);
        free (names->methods[i].name);
        
        free (names->methods[i].params);
        free (names->methods[i].shorty);
        
        free (names->methods[i].return_type);
        
        {
        
            unsigned long j;
            
            for (j = 0; j < names->methods[i].nb_throws_types; j++) {
                free (names->methods[i].throws_types[j]);
            }
            
            free (names->methods[i].throws_types);
        
        }
        
        {
        
            unsigned long j;
            
            for (j = 0; j < names->methods[i].nb_param_names; j++) {
                free (names->methods[i].param_names[j]);
            }
            
            free (names->methods[i].param_names);
        
        }
        
        {
        
            unsigned long j;
            
            for (j = 0; j < names->methods[i].nb_locals; j++) {
            
                free (names->methods[i].locals[j].name);
                free (names->methods[i].locals[j].type);
            
            }
            
            free (names->methods[i].locals);
            
            for (j = 0; j < names->methods[i].nb_statements; j++) {
            
                unsigned long k;
                
                free (names->methods[i].statements[j].target);
                free (names->methods[i].statements[j].method);
                free (names->methods[i].statements[j].arg_text);
                
                free ((char *) names->methods[i].statements[j].filename);
                free ((char *) names->methods[i].statements[j].start);
                
                for (k = 0; k < names->methods[i].statements[j].nb_concat_refs; k++) {
                    free (names->methods[i].statements[j].concat_refs[k].name);
                }
                
                free (names->methods[i].statements[j].concat_refs);
            
            }
            
            free (names->methods[i].statements);
            
            for (j = 0; j < names->methods[i].nb_tries; j++) {
                free (names->methods[i].tries[j].type);
            }
            
            free (names->methods[i].tries);
        
        }
    
    }
    
    free (names->methods);
    
    for (i = 0; i < names->nb_fields; i++) {
    
        free (names->fields[i].name);
        free (names->fields[i].type);
        
        free ((char *) names->fields[i].init_filename);
        free ((char *) names->fields[i].init_start);
        
        free (names->fields[i].init_text);
    
    }
    
    for (i = 0; i < names->nb_nested_classes; i++) {
        free_source_names (&names->nested_classes[i]);
    }

    free (names->nested_classes);
    free (names->fields);
    
    names->package_name = 0;
    names->class_name = 0;
    names->super_class = 0;
    
    names->interfaces = 0;
    names->nb_interfaces = 0;
    
    names->imports = 0;
    names->nb_imports = 0;
    
    names->methods = 0;
    names->nb_methods = 0;
    
    names->fields = 0;
    names->nb_fields = 0;
    
    names->nested_classes = 0;
    names->nb_nested_classes = 0;

}

static void append_text (char **out, unsigned long *len, const char *text) {

    unsigned long text_len = strlen (text);
    char *tmp = xrealloc (*out, *len + text_len + 1);
    
    memcpy (tmp + *len, text, text_len);
    *len += text_len;
    
    tmp[*len] = '\0';
    *out = tmp;

}

static int expect_ident (const char *what) {

    if (tok.kind != TOK_IDENT) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected %s", what);
        return 1;
    
    }
    
    return 0;

}

static int expect_token (enum token_kind kind, const char *what) {

    if (tok.kind != kind) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected %s", what);
        return 1;
    
    }
    
    return 0;

}

static int skip_balanced_parens (void) {

    int depth = 1;
    get_token ();
    
    while (depth > 0) {
    
        if (tok.kind == TOK_EOF) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unexpected end of file in annotation");
            return 1;
        
        }
        
        if (tok.kind == TOK_LPAREN) {
            depth++;
        } else if (tok.kind == TOK_RPAREN) {
            depth--;
        }
        
        get_token ();
    
    }
    
    return 0;

}

static int skip_annotation (void) {

    get_token ();
    
    if (expect_ident ("annotation name")) {
        return 1;
    }
    
    get_token ();
    
    while (tok.kind == TOK_DOT) {
    
        get_token ();
        
        if (expect_ident ("annotation name")) {
            return 1;
        }
        
        get_token ();
    
    }
    
    if (tok.kind == TOK_LPAREN) {
        return skip_balanced_parens ();
    }
    
    return 0;

}

static int skip_annotations (void) {

    while (tok.kind == TOK_AT) {
    
        if (skip_annotation ()) {
            return 1;
        }
    
    }
    
    return 0;

}

static int is_member_modifier (enum token_kind kind) {

    return (kind == TOK_PUBLIC || kind == TOK_PRIVATE || kind == TOK_PROTECTED
        || kind == TOK_STATIC || kind == TOK_FINAL || kind == TOK_VOLATILE
            || kind == TOK_TRANSIENT || kind == TOK_NATIVE
                || kind == TOK_SYNCHRONIZED || kind == TOK_ABSTRACT
                    || kind == TOK_STRICTFP);

}

static void add_import (struct source_names *names, const char *name, int wildcard, int implicit, const char *filename, unsigned long line, const char *source_line, unsigned long caret_offset) {

    struct import_info *imp;
    
    names->imports = xrealloc (names->imports, (names->nb_imports + 1) * sizeof (*names->imports));
    imp = &names->imports[names->nb_imports++];
    
    imp->name = xstrdup (name);
    
    imp->wildcard = wildcard;
    imp->implicit = implicit;
    
    imp->filename = filename ? xstrdup (filename) : 0;
    imp->line = line;
    
    imp->source_line = source_line ? xstrdup (source_line) : 0;
    imp->caret_offset = caret_offset;

}

static void add_interface (struct source_names *names, const char *desc) {

    names->interfaces = xrealloc (names->interfaces, (names->nb_interfaces + 1) * sizeof (*names->interfaces));
    names->interfaces[names->nb_interfaces++] = xstrdup (desc);

}

static char descriptor_shorty_char (const char *desc) {

    if (desc[0] == '[' || desc[0] == 'L') {
        return 'L';
    }
    
    return desc[0];

}

static char *make_method_shorty (const char *return_type, const char *params) {

    unsigned long len = 1;
    
    const char *p;
    char *shorty, *out;
    
    for (p = params; *p; ) {
    
        len++;
        
        while (*p == '[') {
            p++;
        }
        
        if (*p == 'L') {
        
            while (*p && *p != ';') {
                p++;
            }
            
            if (*p == ';') {
                p++;
            }
        
        } else if (*p) {
            p++;
        }
    
    }
    
    shorty = xmalloc (len + 1);
    out = shorty;
    
    *out++ = descriptor_shorty_char (return_type);
    
    for (p = params; *p; ) {
    
        *out++ = 'L';
        
        while (*p == '[') {
            p++;
        }
        
        if (*p == 'L') {
        
            while (*p && *p != ';') {
                p++;
            }
            
            if (*p == ';') {
                p++;
            }
        
        } else if (*p) {
        
            out[-1] = *p;
            p++;
        
        }
    
    }
    
    *out = '\0';
    return shorty;

}

static void append_jni_name_char (char **out, unsigned long *len, char ch) {

    char tmp[8];
    unsigned int v;
    
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
    
        tmp[0] = ch;
        tmp[1] = '\0';
        
        append_text (out, len, tmp);
        return;
    
    }
    
    if (ch == '.') {
        append_text (out, len, "_");
    } else if (ch == '_') {
        append_text (out, len, "_1");
    } else if (ch == ';') {
        append_text (out, len, "_2");
    } else if (ch == '[') {
        append_text (out, len, "_3");
    } else {
    
        v = (unsigned char) ch;
        sprintf (tmp, "_0%04x", v);
        
        append_text (out, len, tmp);
    
    }

}

static void append_jni_name_text (char **out, unsigned long *len, const char *text) {

    const char *p;
    
    for (p = text; *p; p++) {
        append_jni_name_char (out, len, *p);
    }

}

static char *make_jni_native_name (struct source_names *names, const char *method_name) {

    char *out = 0;
    unsigned long len = 0;
    
    append_text (&out, &len, "Java_");
    
    if (names->package_name && *names->package_name) {
    
        append_jni_name_text (&out, &len, names->package_name);
        append_text (&out, &len, "_");
    
    }
    
    append_jni_name_text (&out, &len, names->class_name);
    append_text (&out, &len, "_");
    append_jni_name_text (&out, &len, method_name);
    
    return out;

}

static void add_method (struct source_names *names, const char *name, const char *return_type, const char *params, char **param_names, unsigned long nb_param_names, unsigned int flags) {

    struct method_info *method;
    char *shorty;
    char *dex_name = 0;
    
    names->methods = xrealloc (names->methods, (names->nb_methods + 1) * sizeof (*names->methods));
    method = &names->methods[names->nb_methods++];
    
    shorty = make_method_shorty (return_type, params);
    
    if (flags & METHOD_NATIVE) {
        dex_name = make_jni_native_name (names, name);
    }
    
    method->name = dex_name ? dex_name : xstrdup (name);
    method->java_name = xstrdup (name);
    
    method->return_type = xstrdup (return_type);
    
    method->shorty = shorty;
    method->params = xstrdup (params);
    
    method->flags = flags;
    
    method->throws_types = 0;
    method->nb_throws_types = 0;
    
    method->param_names = param_names;
    method->nb_param_names = nb_param_names;
    
    method->locals = 0;
    method->nb_locals = 0;
    
    method->local_scope_depth = 0;
    
    method->statements = 0;
    method->nb_statements = 0;
    
    method->tries = 0;
    method->nb_tries = 0;
    method->try_depth = 0;

}

static void add_field (struct source_names *names, const char *name, const char *type, const char *init_text, unsigned long init_line, const char *init_start, const char *init_caret, int is_constructor_initializer, unsigned int flags) {

    struct field_info *field;
    
    names->fields = xrealloc (names->fields, (names->nb_fields + 1) * sizeof (*names->fields));
    field = &names->fields[names->nb_fields++];
    
    field->name = xstrdup (name);
    field->type = xstrdup (type);
    
    field->init_text = init_text ? xstrdup (init_text) : 0;
    field->has_initializer = init_text != 0;
    
    field->is_constructor_initializer = is_constructor_initializer;
    
    field->init_filename = init_text ? xstrdup (get_filename ()) : 0;
    field->init_line = init_text ? init_line : 0;
    
    field->init_start = init_start ? xstrdup (init_start) : 0;
    field->init_caret = field->init_start ? field->init_start + (init_caret - init_start) : 0;
    
    field->flags = flags;

}

static void report_field_init_error (struct field_info *field, const char *message) {

    if (field && field->init_start) {
        report_line_at (field->init_filename, field->init_line, REPORT_ERROR, field->init_start, field->init_caret, "%s", message);
    } else {
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "%s", message);
    }

}

static void report_unknown_static_field_method (struct field_info *field, const char *target, const char *method) {

    char *message;
    unsigned long len;
    
    len = strlen ("unknown static method ''") + strlen (target) + 1 + strlen (method);
    message = xmalloc (len + 1);
    
    sprintf (message, "unknown static method '%s.%s'", target, method);
    report_field_init_error (field, message);
    
    free (message);

}

static void add_call_statement (struct method_info *method, const char *target, const char *name, int arg_kind, const char *arg_text, unsigned long arg_param_index, unsigned long line, const char *start_pos, const char *caret, struct concat_field_ref *concat_refs, unsigned long nb_concat_refs) {

    struct statement_info *stmt;
    unsigned long i;
    
    method->statements = xrealloc (method->statements, (method->nb_statements + 1) * sizeof (*method->statements));
    stmt = &method->statements[method->nb_statements++];
    
    memset (stmt, 0, sizeof (*stmt));
    
    stmt->kind = STMT_CALL;
    stmt->arg_kind = arg_kind;
    
    stmt->target = target ? xstrdup (target) : 0;
    stmt->method = name ? xstrdup (name) : 0;
    
    stmt->arg_param_index = arg_param_index;
    stmt->arg_text = arg_text ? xstrdup (arg_text) : 0;
    
    stmt->filename = xstrdup (get_filename ());
    stmt->line = line;
    
    stmt->start = start_pos;
    stmt->caret = caret;
    
    if (nb_concat_refs) {
    
        stmt->concat_refs = xmalloc (nb_concat_refs * sizeof (*stmt->concat_refs));
        stmt->nb_concat_refs = nb_concat_refs;
        
        for (i = 0; i < nb_concat_refs; i++) {
        
            stmt->concat_refs[i].name = xstrdup (concat_refs[i].name);
            
            stmt->concat_refs[i].filename = xstrdup (concat_refs[i].filename);
            stmt->concat_refs[i].line = concat_refs[i].line;
            
            stmt->concat_refs[i].caret = (stmt->concat_refs[i].start = xstrdup (concat_refs[i].start)) + (concat_refs[i].caret - concat_refs[i].start);
        
        }
    
    }

}

static void add_concat_field_ref (struct concat_field_ref **refs, unsigned long *nb_refs, const char *name, unsigned long line, const char *start_pos, const char *caret) {

    struct concat_field_ref *ref;
    
    *refs = xrealloc (*refs, (*nb_refs + 1) * sizeof (**refs));
    ref = &(*refs)[(*nb_refs)++];
    
    ref->name = xstrdup (name);
    
    ref->filename = xstrdup (get_filename ());
    ref->line = line;
    
    ref->start = start_pos;
    ref->caret = caret;

}

static void free_concat_field_refs (struct concat_field_ref *refs, unsigned long nb_refs) {

    unsigned long i;
    
    for (i = 0; i < nb_refs; i++) {
        free (refs[i].name);
    }
    
    free (refs);

}

static char *copy_string_literal (const char *text) {

    unsigned long len = strlen (text);
    unsigned long i;
    
    char *out;
    char *p;
    
    if (len >= 2 && text[0] == '"' && text[len - 1] == '"') {
    
        text++;
        len -= 2;
    
    }
    
    out = xmalloc (len + 1);
    p = out;
    
    for (i = 0; i < len; i++) {
    
        if (text[i] == '\\' && i + 1 < len) {
        
            i++;
            
            if (text[i] == 'n') {
                *p++ = '\n';
            } else if (text[i] == 't') {
                *p++ = '\t';
            } else if (text[i] == 'r') {
                *p++ = '\r';
            } else {
                *p++ = text[i];
            }
            
            continue;
        
        }
        
        *p++ = text[i];
    
    }
    
    *p = '\0';
    return out;

}

static long character_literal_to_long (const char *text) {

    const unsigned char *p = (const unsigned char *) text;
    
    unsigned long value = 0;
    int digits;
    
    if (*p == '\'') {
        p++;
    }
    
    if (*p != '\\') {
        return *p;
    }
    
    p++;
    
    switch (*p) {
    
        case 'b':
        
            return '\b';
        
        case 't':
        
            return '\t';
        
        case 'n':
        
            return '\n';
        
        case 'f':
        
            return '\f';
        
        case 'r':
        
            return '\r';
        
        case '\"':
        
            return '\"';
        
        case '\'':
        
            return '\'';
        
        case '\\':
        
            return '\\';
        
        case 'u':
        
            do {
                p++;
            } while (*p == 'u');
            
            for (digits = 0; digits < 4; digits++, p++) {
            
                value <<= 4;
                
                if (*p >= '0' && *p <= '9') {
                    value |= *p - '0';
                } else if (*p >= 'a' && *p <= 'f') {
                    value |= *p - 'a' + 10;
                } else if (*p >= 'A' && *p <= 'F') {
                    value |= *p - 'A' + 10;
                } else {
                    break;
                }
            
            }
            
            return (long) value;
        
        default:
        
            if (*p >= '0' && *p <= '7') {
            
                digits = *p <= '3' ? 3 : 2;
                
                while (digits-- && *p >= '0' && *p <= '7') {
                    value = (value << 3) | (*p++ - '0');
                }
                
                return (long) value;
            
            }
            
            return *p;
    
    }

}

static int has_constructor (struct source_names *names) {

    unsigned long i;
    
    for (i = 0; i < names->nb_methods; i++) {
    
        if (strcmp (names->methods[i].name, "<init>") == 0) {
            return 1;
        }
    
    }
    
    return 0;

}

static char *qualified_to_descriptor (const char *name) {

    unsigned long len = strlen (name);
    
    char *desc = xmalloc (len + 3);
    char *p = desc;
    
    const char *q;
    *p++ = 'L';
    
    for (q = name; *q; q++) {
        *p++ = (*q == '.') ? '/' : *q;
    }
    
    *p++ = ';';
    *p = '\0';
    
    return desc;

}

static int declared_type_available (const char *descriptor) {

    unsigned long i;
    
    if (strcmp (descriptor, JAVA_OBJECT_TYPE) == 0 || strcmp (descriptor, JAVA_STRING_TYPE) == 0) {
        return 1;
    }
    
    for (i = 0; i < nb_declaration_names; i++) {
    
        char *candidate = dex_class_descriptor (&declaration_names[i]);
        int match = strcmp (candidate, descriptor) == 0;
        
        free (candidate);
        
        if (match) {
            return 1;
        
        }
    
    }
    
    return 0;

}

static const char *simple_name (const char *name) {

    const char *dot = strrchr (name, '.');
    return dot ? dot + 1 : name;

}

static char *resolve_class_type (struct source_names *names, const char *name) {

    unsigned long i;
    char *full;
    
    if (strchr (name, '.')) {
        return qualified_to_descriptor (name);
    }
    
    if (strcmp (name, names->class_name) == 0) {
        return dex_class_descriptor (names);
    }
    
    for (i = 0; i < names->nb_imports; i++) {
    
        if (!names->imports[i].wildcard && strcmp (simple_name (names->imports[i].name), name) == 0) {
            return qualified_to_descriptor (names->imports[i].name);
        }
    
    }
    
    for (i = 0; i < nb_declaration_names; i++) {
    
        struct source_names *candidate = &declaration_names[i];
        
        const char *candidate_package = candidate->package_name ? candidate->package_name : "";
        const char *current_package = names->package_name ? names->package_name : "";
        
        if (strcmp (candidate->class_name, name) == 0 && strcmp (candidate_package, current_package) == 0) {
            return dex_class_descriptor (candidate);
        }
    
    }
    
    if (strcmp (name, "Object") == 0) {
        return xstrdup (JAVA_OBJECT_TYPE);
    }
    
    if (strcmp (name, "String") == 0) {
        return xstrdup (JAVA_STRING_TYPE);
    }
    
    for (i = 0; i < nb_declaration_names; i++) {
    
        struct source_names *candidate = &declaration_names[i];
        
        if (candidate->package_name && strcmp (candidate->package_name, "java.lang") == 0 && strcmp (candidate->class_name, name) == 0) {
            return dex_class_descriptor (candidate);
        }
    
    }
    
    for (i = 0; i < names->nb_imports; i++) {
    
        unsigned long j;
        
        if (!names->imports[i].wildcard) {
            continue;
        }
        
        for (j = 0; j < nb_declaration_names; j++) {
        
            struct source_names *candidate = &declaration_names[j];
            
            if (candidate->package_name && strcmp (candidate->package_name, names->imports[i].name) == 0 && strcmp (candidate->class_name, name) == 0) {
                return dex_class_descriptor (candidate);
            }
        
        }
    
    }
    
    for (i = 0; i < names->nb_imports; i++) {
    
        if (names->imports[i].wildcard) {
        
            unsigned long len = strlen (names->imports[i].name) + 1 + strlen (name);
            
            full = xmalloc (len + 1);
            sprintf (full, "%s.%s", names->imports[i].name, name);
            
            {
            
                char *desc = qualified_to_descriptor (full);
                free (full);
                
                return desc;
            
            }
        
        }
    
    }
    
    if (names->package_name && *names->package_name) {
    
        unsigned long len = strlen (names->package_name) + 1 + strlen (name);
        
        full = xmalloc (len + 1);
        sprintf (full, "%s.%s", names->package_name, name);
        
        {
        
            char *desc = qualified_to_descriptor (full);
            free (full);
            
            return desc;
        
        }
    
    }
    
    return qualified_to_descriptor (name);

}

static const char *type_descriptor (enum token_kind kind) {

    switch (kind) {
    
        case TOK_VOID:
        
            return "V";
        
        case TOK_BOOLEAN:
        
            return "Z";
        
        case TOK_BYTE:
        
            return "B";
        
        case TOK_CHAR:
        
            return "C";
        
        case TOK_SHORT:
        
            return "S";
        
        case TOK_INT:
        
            return "I";
        
        case TOK_LONG:
        
            return "J";
        
        case TOK_FLOAT:
        
            return "F";
        
        case TOK_DOUBLE:
        
            return "D";
        
        default:
        
            return 0;
    
    }

}

static int append_array_suffix (unsigned long *array_depth) {

    while (tok.kind == TOK_LBRACK) {
    
        get_token ();
        
        if (expect_token (TOK_RBRACK, "']'")) {
            return 1;
        }
        
        (*array_depth)++;
        get_token ();
    
    }
    
    return 0;

}

static char *apply_array_suffix (char *base, unsigned long array_depth) {

    char *desc;
    unsigned long len, i;
    
    if (!array_depth) {
        return base;
    }
    
    len = strlen (base);
    desc = xmalloc (array_depth + len + 1);
    
    for (i = 0; i < array_depth; i++) {
        desc[i] = '[';
    }
    
    memcpy (desc + array_depth, base, len + 1);
    free (base);
    
    return desc;

}

static int parse_type_name (struct source_names *names, int allow_void, char **out_desc) {

    const char *prim;
    
    char *desc = 0;
    char *name = 0;
    
    unsigned long len = 0;
    unsigned long array_depth = 0;
    
    prim = type_descriptor (tok.kind);
    
    if (prim) {
    
        if (!allow_void && tok.kind == TOK_VOID) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "void is not valid as a parameter type");
            return 1;
        
        }
        
        desc = xstrdup (prim);
        get_token ();
    
    } else if (tok.kind == TOK_IDENT) {
    
        const char *type_start = tok.start;
        const char *type_caret = tok.caret;
        
        unsigned long type_line = get_line_number ();
        append_text (&name, &len, tok.ident);
        
        get_token ();
        
        while (tok.kind == TOK_DOT) {
        
            append_text (&name, &len, ".");
            get_token ();
            
            if (expect_ident ("identifier in type name")) {
            
                free (name);
                return 1;
            }
            
            append_text (&name, &len, tok.ident);
            get_token ();
        
        }
        
        desc = resolve_class_type (names, name);
        
        if (!collect_declarations_only && nb_declaration_names && !declared_type_available (desc)) {
        
            report_line_at (get_filename (), type_line, REPORT_ERROR, type_start, type_caret,
                "unknown class '%s'", name);
            
            free (desc);
            free (name);
            
            return 1;
        
        }
        
        free (name);
    
    } else {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected type name");
        return 1;
    
    }
    
    if (tok.kind == TOK_LBRACK) {
    
        if (desc[0] == 'V') {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "void array is not valid");
            
            free (desc);
            return 1;
        
        }
        
        if (append_array_suffix (&array_depth)) {
        
            free (desc);
            return 1;
        
        }
    
    }
    
    *out_desc = apply_array_suffix (desc, array_depth);
    return 0;

}

static int parse_param_list (struct source_names *names, char **params_out, char ***param_names_out, unsigned long *nb_param_names_out) {

    char *params = 0;
    unsigned long len = 0;
    
    *params_out = 0;
    *param_names_out = 0;
    *nb_param_names_out = 0;
    
    if (expect_token (TOK_LPAREN, "'('") ) {
        return 1;
    }
    
    get_token ();
    
    while (tok.kind != TOK_RPAREN) {
    
        unsigned long name_array_depth = 0;
        char *desc;
        
        if (tok.kind == TOK_EOF) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unexpected end of file in parameter list");
            
            free (params);
            return 1;
        
        }
        
        if (parse_type_name (names, 0, &desc)) {
        
            free (params);
            return 1;
        
        }
        
        if (expect_ident ("parameter name")) {
        
            free (params);
            free (desc);
            
            return 1;
        
        }
        
        *param_names_out = xrealloc (*param_names_out, (*nb_param_names_out + 1) * sizeof (**param_names_out));
        (*param_names_out)[(*nb_param_names_out)++] = xstrdup (tok.ident);
        
        get_token ();
        
        if (append_array_suffix (&name_array_depth)) {
        
            free (params);
            free (desc);
            
            return 1;
        
        }
        
        desc = apply_array_suffix (desc, name_array_depth);
        append_text (&params, &len, desc);
        
        free (desc);
        
        if (tok.kind == TOK_COMMA) {
        
            get_token ();
            continue;
        
        }
        
        if (tok.kind != TOK_RPAREN) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected ',' or ')' in parameter list");
            
            free (params);
            return 1;
        
        }
    
    }
    
    if (!params) {
        params = xstrdup ("");
    }
    
    *params_out = params;
    return 0;

}

static int find_param_index (struct method_info *method, const char *name, unsigned long *index_out) {

    unsigned long i;
    
    for (i = 0; i < method->nb_param_names; i++) {
    
        if (strcmp (method->param_names[i], name) == 0) {
        
            *index_out = i;
            return 0;
        
        }
    
    }
    
    return 1;

}

static void free_param_names (char **param_names, unsigned long nb_param_names) {

    unsigned long i;
    
    for (i = 0; i < nb_param_names; i++) {
        free (param_names[i]);
    }
    
    free (param_names);

}

static struct field_info *find_field_info_by_name (struct source_names *names, const char *name) {

    const char *super_desc;
    unsigned long i, depth;
    
    for (i = 0; i < names->nb_fields; i++) {
    
        if (strcmp (names->fields[i].name, name) == 0) {
            return &names->fields[i];
        }
    
    }
    
    super_desc = names->super_class;
    
    for (depth = 0; super_desc && depth < nb_declaration_names; depth++) {
    
        struct source_names *parent = 0;
        unsigned long j;
        
        for (i = 0; i < nb_declaration_names; i++) {
        
            char *desc = dex_class_descriptor (&declaration_names[i]);
            int match = strcmp (desc, super_desc) == 0;
            
            free (desc);
            
            if (match) {
            
                parent = &declaration_names[i];
                break;
            
            }
        
        }
        
        if (!parent) {
            break;
        }
        
        for (j = 0; j < parent->nb_fields; j++) {
        
            struct field_info *field = &parent->fields[j];
            
            const char *current_package = names->package_name ? names->package_name : "";
            const char *parent_package = parent->package_name ? parent->package_name : "";
            
            if (strcmp (field->name, name) == 0
                && !(field->flags & METHOD_PRIVATE)
                    && ((field->flags & (METHOD_PUBLIC | METHOD_PROTECTED))
                        || strcmp (current_package, parent_package) == 0)) {
                return field;
            }
        
        }
        
        super_desc = parent->super_class;
    
    }
    
    return 0;

}

static struct field_info *find_static_field_info_by_target (struct source_names *names, const char *target, char **owner_desc_out) {

    const char *dot = strrchr (target, '.');
    char *class_name, *class_desc;
    
    struct field_info *result = 0;
    unsigned long depth, i;
    
    *owner_desc_out = 0;
    
    if (!dot) {
        return 0;
    }
    
    class_name = xmalloc ((unsigned long) (dot - target) + 1);
    memcpy (class_name, target, (unsigned long) (dot - target));
    
    class_name[dot - target] = '\0';
    
    class_desc = resolve_class_type (names, class_name);
    free (class_name);
    
    for (depth = 0; class_desc && depth < nb_declaration_names; depth++) {
    
        struct source_names *owner = 0;
        
        for (i = 0; i < nb_declaration_names; i++) {
        
            char *desc = dex_class_descriptor (&declaration_names[i]);
            int match = strcmp (desc, class_desc) == 0;
            
            free (desc);
            
            if (match) {
            
                owner = &declaration_names[i];
                break;
            
            }
        
        }
        
        if (!owner) {
            break;
        }
        
        for (i = 0; i < owner->nb_fields; i++) {
        
            struct field_info *field = &owner->fields[i];
            
            const char *current_package = names->package_name ? names->package_name : "";
            const char *owner_package = owner->package_name ? owner->package_name : "";
            
            if (strcmp (field->name, dot + 1) == 0) {
            
                if ((field->flags & METHOD_STATIC)
                    && ((strcmp (owner->class_name, names->class_name) == 0
                        && strcmp (current_package, owner_package) == 0)
                            || (!(field->flags & METHOD_PRIVATE)
                                && ((field->flags & (METHOD_PUBLIC | METHOD_PROTECTED))
                                    || strcmp (current_package, owner_package) == 0)))) {
                    result = field;
                    *owner_desc_out = class_desc;
                    return result;
                }
                
                goto done;
            
            }
        
        }
        
        {
        
            char *parent_desc = owner->super_class ? xstrdup (owner->super_class) : 0;
            free (class_desc);
            class_desc = parent_desc;
        
        }
    
    }
    
done:
    
    free (class_desc);
    return 0;

}

static struct method_info *find_method_info_by_java_name (struct source_names *names, const char *name) {

    unsigned long i;
    
    for (i = 0; i < names->nb_methods; i++) {
    
        if (strcmp (names->methods[i].java_name, name) == 0) {
            return &names->methods[i];
        }
    
    }
    
    return 0;

}

static struct method_info *find_declared_unqualified_method_info (struct source_names *names, const char *method_name, const char *params) {

    char *current = dex_class_descriptor (names);
    unsigned long depth;
    
    struct method_info *result = 0;
    
    for (depth = 0; current && depth < nb_declaration_names; depth++) {
    
        const char *super_type = 0;
        unsigned long i, j;
        
        for (i = 0; i < nb_declaration_names; i++) {
        
            char *desc = dex_class_descriptor (&declaration_names[i]);
            
            int match = strcmp (desc, current) == 0;
            free (desc);
            
            if (!match) {
                continue;
            }
            
            super_type = declaration_names[i].super_class;
            
            for (j = 0; j < declaration_names[i].nb_methods; j++) {
            
                struct method_info *candidate = &declaration_names[i].methods[j];
                
                const char *current_package = names->package_name ? names->package_name : "";
                const char *declared_package = declaration_names[i].package_name ? declaration_names[i].package_name : "";
                
                if (strcmp (candidate->java_name, method_name) == 0
                    && strcmp (candidate->params, params) == 0
                        && (depth == 0 || (!(candidate->flags & METHOD_PRIVATE)
                            && ((candidate->flags & (METHOD_PUBLIC | METHOD_PROTECTED))
                                || strcmp (current_package, declared_package) == 0)))) {
                    result = candidate;
                    break;
                
                }
            
            }
            
            break;
        
        }
        
        if (result || i == nb_declaration_names || !super_type || strcmp (super_type, current) == 0) {
            break;
        }
        
        {
        
            char *next = xstrdup (super_type);
            free (current);
            
            current = next;
        
        }
    
    }
    
    free (current);
    return result;

}

static const char *lookup_declared_method_return_type (const char *class_desc, const char *method_name, const char *params, int is_static) {

    unsigned long i, j;
    
    for (i = 0; i < nb_declaration_names; i++) {
    
        struct source_names *names = &declaration_names[i];
        
        char *desc = dex_class_descriptor (names);
        int matching = strcmp (desc, class_desc) == 0;
        
        free (desc);
        
        if (!matching) {
            continue;
        }
        
        for (j = 0; j < names->nb_methods; j++) {
        
            struct method_info *method = &names->methods[j];
            
            if (!!(method->flags & METHOD_STATIC) == is_static
                && strcmp (method->java_name, method_name) == 0
                    && strcmp (method->params, params) == 0) {
                return method->return_type;
            }
        
        }
        
        if (names->super_class && strcmp (names->super_class, class_desc) != 0) {
            return lookup_declared_method_return_type (names->super_class, method_name, params, is_static);
        }
        
        break;
    
    }
    
    return 0;

}

static void parse_integer_text64 (int64_s *out, const char *text) {

    char tmp[128];
    
    unsigned long len = strlen (text);
    int neg = 0;
    
    if (len >= sizeof (tmp)) {
        len = sizeof (tmp) - 1;
    }
    
    memcpy (tmp, text, len);
    tmp[len] = '\0';
    
    if (tmp[0] == '-') {
        neg = 1;
    }
    
    if (len > (unsigned long) neg && (tmp[len - 1] == 'l' || tmp[len - 1] == 'L')) {
        tmp[len - 1] = '\0';
    }
    
    parse_string_to_i64 (out, tmp + neg);
    
    if (neg) {
        neg64 (out);
    }

}

#define     FIELD_INIT_CALL_PREFIX                          "@call|"
#define     FIELD_INIT_MUL_DI_PREFIX                        "@muldi|"
#define     FIELD_INIT_MUL_FIELD_PREFIX                     "@mulfield|"

static char *make_field_call_initializer_text (const char *target, const char *method) {

    unsigned long len;
    char *text;
    
    len = strlen (FIELD_INIT_CALL_PREFIX) + strlen (target) + 1 + strlen (method);
    text = xmalloc (len + 1);
    
    sprintf (text, "%s%s|%s", FIELD_INIT_CALL_PREFIX, target, method);
    return text;

}

static char *make_field_mul_di_initializer_text (const char *target, const char *method, const char *factor) {

    unsigned long len;
    char *text;
    
    len = strlen (FIELD_INIT_MUL_DI_PREFIX) + strlen (target) + 1 + strlen (method) + 1 + strlen (factor);
    text = xmalloc (len + 1);
    
    sprintf (text, "%s%s|%s|%s", FIELD_INIT_MUL_DI_PREFIX, target, method, factor);
    return text;

}

static char *make_field_mul_field_initializer_text (const char *field, const char *factor) {

    unsigned long len;
    char *text;
    
    len = strlen (FIELD_INIT_MUL_FIELD_PREFIX) + strlen (field) + 1 + strlen (factor);
    text = xmalloc (len + 1);
    
    sprintf (text, "%s%s|%s", FIELD_INIT_MUL_FIELD_PREFIX, field, factor);
    return text;

}

static int field_initializer_is_mul_di (const char *text) {
    return text && strncmp (text, FIELD_INIT_MUL_DI_PREFIX, strlen (FIELD_INIT_MUL_DI_PREFIX)) == 0;
}

static int field_initializer_is_mul_field (const char *text) {
    return text && strncmp (text, FIELD_INIT_MUL_FIELD_PREFIX, strlen (FIELD_INIT_MUL_FIELD_PREFIX)) == 0;
}

static int field_initializer_is_call (const char *text) {
    return text && strncmp (text, FIELD_INIT_CALL_PREFIX, strlen (FIELD_INIT_CALL_PREFIX)) == 0;
}

static int split_field_call_initializer (const char *text, char **target_out, char **method_out) {

    const char *p;
    const char *sep;
    
    unsigned long target_len;
    
    *target_out = 0;
    *method_out = 0;
    
    if (!field_initializer_is_call (text)) {
        return 1;
    }
    
    p = text + strlen (FIELD_INIT_CALL_PREFIX);
    sep = strchr (p, '|');
    
    if (!sep || sep == p || !sep[1]) {
        return 1;
    }
    
    target_len = (unsigned long) (sep - p);
    *target_out = xmalloc (target_len + 1);
    
    memcpy (*target_out, p, target_len);
    (*target_out)[target_len] = '\0';
    
    *method_out = xstrdup (sep + 1);
    return 0;

}

static int split_field_mul_di_initializer (const char *text, char **target_out, char **method_out, char **factor_out) {

    const char *p;
    const char *sep1;
    const char *sep2;
    
    unsigned long target_len;
    unsigned long method_len;
    
    *target_out = 0;
    *method_out = 0;
    *factor_out = 0;
    
    if (!field_initializer_is_mul_di (text)) {
        return 1;
    }
    
    p = text + strlen (FIELD_INIT_MUL_DI_PREFIX);
    sep1 = strchr (p, '|');
    
    if (!sep1 || sep1 == p || !sep1[1]) {
        return 1;
    }
    
    sep2 = strchr (sep1 + 1, '|');
    
    if (!sep2 || sep2 == sep1 + 1 || !sep2[1]) {
        return 1;
    }
    
    target_len = (unsigned long) (sep1 - p);
    method_len = (unsigned long) (sep2 - sep1 - 1);
    
    *target_out = xmalloc (target_len + 1);
    memcpy (*target_out, p, target_len);
    
    (*target_out)[target_len] = '\0';
    
    *method_out = xmalloc (method_len + 1);
    memcpy (*method_out, sep1 + 1, method_len);
    
    (*method_out)[method_len] = '\0';
    
    *factor_out = xstrdup (sep2 + 1);
    return 0;

}

#define     CONCAT_LITERAL_PREFIX       "S:"
#define     CONCAT_FIELD_PREFIX         "F:"
#define     CONCAT_MUL_FIELD_PREFIX     "M:"
#define     CONCAT_LOCAL_PREFIX         "L:"
#define     CONCAT_PARAM_PREFIX         "P:"
#define     CONCAT_SEP                  "\037"

static void concat_append_literal (char **out, unsigned long *len, const char *text) {

    append_text (out, len, CONCAT_LITERAL_PREFIX);
    append_text (out, len, text);
    append_text (out, len, CONCAT_SEP);

}

static void concat_append_field (char **out, unsigned long *len, const char *name) {

    append_text (out, len, CONCAT_FIELD_PREFIX);
    append_text (out, len, name);
    append_text (out, len, CONCAT_SEP);

}

static void concat_append_param (char **out, unsigned long *len, unsigned long param_index) {

    char tmp[64];
    sprintf (tmp, "%lu", param_index);
    
    append_text (out, len, CONCAT_PARAM_PREFIX);
    append_text (out, len, tmp);
    append_text (out, len, CONCAT_SEP);

}

static void concat_append_local (char **out, unsigned long *len, unsigned long local_index) {

    char tmp[64];
    sprintf (tmp, "%lu", local_index);
    
    append_text (out, len, CONCAT_LOCAL_PREFIX);
    append_text (out, len, tmp);
    append_text (out, len, CONCAT_SEP);

}

static long field_initializer_to_long (const char *text) {

    int64_s value;
    
    parse_integer_text64 (&value, text);
    return (long) value.low;

}

static struct local_info *find_local_info_by_name (struct method_info *method, const char *name, unsigned long *index_out) {

    unsigned long i;
    
    for (i = method->nb_locals; i > 0; i--) {
    
        if (method->locals[i - 1].active && strcmp (method->locals[i - 1].name, name) == 0) {
        
            if (index_out) {
                *index_out = i - 1;
            }
            
            return &method->locals[i - 1];
        
        }
    
    }
    
    return 0;

}

static void leave_local_scope (struct method_info *method) {

    unsigned long i;
    
    for (i = 0; i < method->nb_locals; i++) {
    
        if (method->locals[i].scope_depth == method->local_scope_depth) {
            method->locals[i].active = 0;
        }
    
    }
    
    method->local_scope_depth--;

}

static int field_has_prior_assignment (struct method_info *method, const char *name) {

    unsigned long i;
    
    for (i = 0; i < method->nb_statements; i++) {
    
        struct statement_info *stmt = &method->statements[i];
        
        if (stmt->kind == STMT_LOCAL && stmt->target && stmt->target[0] != '$' && strcmp (stmt->target, name) == 0) {
            return 1;
        }
   
    }
    
    return 0;

}

static void add_array_put_statement (struct method_info *method, unsigned long array_local, unsigned long index_local, unsigned long value_local) {

    struct statement_info *stmt;
    char array_text[64], index_text[64], value_text[64];
    
    method->statements = xrealloc (method->statements, (method->nb_statements + 1) * sizeof (*method->statements));
    stmt = &method->statements[method->nb_statements++];
    
    memset (stmt, 0, sizeof (*stmt));

    sprintf (array_text, "$%lu", array_local);
    sprintf (index_text, "$%lu", index_local);
    sprintf (value_text, "$%lu", value_local);
    
    stmt->kind = STMT_ARRAY_PUT;
    stmt->target = xstrdup (array_text);
    stmt->method = xstrdup (index_text);
    stmt->arg_text = xstrdup (value_text);

}

static const char *expression_type_descriptor (char type);
static const char *expression_argument_type (struct source_names *names, struct method_info *method, const struct field_mul_expr *expr);

static char *copy_first_type_descriptor (const char *type);
static int add_constructor_argument_local (struct source_names *names, struct method_info *method, struct field_mul_expr *expr, unsigned long *local_index_out);

static int parse_body_expression (struct source_names *names, struct method_info *method, struct field_mul_expr *expr);
static void free_field_mul_expr (struct field_mul_expr *expr);

static int add_local_info (struct method_info *method, const char *name, const char *type, unsigned long *index_out);
static void add_local_statement (struct method_info *method, unsigned long local_index, int arg_kind, const char *source, const char *operations);

static int parse_concat_instance_call (struct method_info *method, const char *target, char **concat, unsigned long *concat_len);
static int parse_concat_static_call (struct method_info *method, const char *target, char **concat, unsigned long *concat_len);

static int declared_reference_assignable (const char *actual, const char *formal);
static int primitive_widening_assignable (char actual, char formal);

static const char *method_param_type (struct method_info *method, unsigned long param_index);

static int parse_concat_static_call (struct method_info *method, const char *target, char **concat, unsigned long *concat_len) {

    char *method_name;
    char result_name[64];
    char result_target[64];
    
    unsigned long result_local;
    
    const char *call_start;
    const char *call_caret;
    
    unsigned long call_line;
    
    call_caret = ((call_start = xstrdup (tok.start)) + (tok.caret - tok.start));
    call_line = get_line_number ();
    
    if (expect_token (TOK_DOT, "'.' after class name")) {
    
        free ((char *) call_start);
        return 1;
    
    }
    
    get_token ();
    
    if (expect_ident ("method name after '.'")) {
    
        free ((char *) call_start);
        return 1;
    
    }
    
    method_name = xstrdup (tok.ident);
    get_token ();
    
    if (expect_token (TOK_LPAREN, "'(' after method name")) {
    
        free ((char *) call_start);
        free (method_name);
        
        return 1;
    
    }
    
    get_token ();
    
    if (expect_token (TOK_RPAREN, "')' after method arguments")) {
    
        free ((char *) call_start);
        free (method_name);
        
        return 1;
    
    }
    
    get_token ();
    sprintf (result_name, "__j2d_call_result_%lu", method->nb_locals);
    
    if (add_local_info (method, result_name, "I", &result_local)) {
    
        free ((char *) call_start);
        free (method_name);
        
        return 1;
    
    }
    
    sprintf (result_target, "$%lu", result_local);
    
    add_call_statement (method, target, method_name, ARG_RESULT_CALL, result_target, 0, call_line, call_start, call_caret, 0, 0);
    concat_append_local (concat, concat_len, result_local);
    
    free (method_name);
    return 0;

}

static int field_has_prior_lazy_concat_init (struct method_info *method, const char *name) {

    unsigned long i;
    unsigned long name_len = strlen (name);
    
    for (i = 0; i < method->nb_statements; i++) {
    
        struct statement_info *stmt = &method->statements[i];
        const char *p;
        
        if (stmt->kind != STMT_CALL || stmt->arg_kind != ARG_STRING_CONCAT || !stmt->arg_text) {
            continue;
        }
        
        p = stmt->arg_text;
        
        while (*p) {
        
            const char *q = strchr (p, '\037');
            unsigned long len;
            
            if (!q) {
                q = p + strlen (p);
            }
            
            len = (unsigned long) (q - p);
            
            if (len == name_len + 2 && p[0] == 'M' && p[1] == ':'
                && memcmp (p + 2, name, name_len) == 0) {
                return 1;
            }
            
            p = *q == '\037' ? q + 1 : q;
        
        }
    
    }
    
    return 0;

}

static int parse_concat_instance_call (struct method_info *method, const char *target, char **concat, unsigned long *concat_len) {

    char *method_name;
    char result_name[64];
    char result_target[64];
    
    unsigned long result_local;
    
    const char *call_start;
    const char *call_caret;
    
    unsigned long call_line;
    
    call_caret = ((call_start = xstrdup (tok.start)) + (tok.caret - tok.start));
    call_line = get_line_number ();
    
    get_token ();
    
    if (expect_ident ("method name after '.'")) {
    
        free ((char *) call_start);
        return 1;
    
    }
    
    method_name = xstrdup (tok.ident);
    get_token ();
    
    if (expect_token (TOK_LPAREN, "'(' after method name")) {
    
        free ((char *) call_start);
        free (method_name);
        
        return 1;
    
    }
    
    get_token ();
    
    if (expect_token (TOK_RPAREN, "')' after method arguments")) {
    
        free ((char *) call_start);
        free (method_name);
        
        return 1;
    
    }
    
    get_token ();
    sprintf (result_name, "__j2d_call_result_%lu", method->nb_locals);
    
    if (add_local_info (method, result_name, "I", &result_local)) {
    
        free ((char *) call_start);
        free (method_name);
        
        return 1;
    
    }
    
    sprintf (result_target, "$%lu", result_local);
    add_call_statement (method, target, method_name, ARG_RESULT_CALL, result_target, 0, call_line, call_start, call_caret, 0, 0);
    
    concat_append_local (concat, concat_len, result_local);
    
    free (method_name);
    return 0;

}

static int parse_concat_array_get (struct source_names *names, struct method_info *method, const char *array_source, const char *array_type, unsigned long *result_local_out) {

    struct field_mul_expr index_expr;
    unsigned long index_local;
    
    char index_source[64];
    char temp_name[64];
    
    get_token ();
    
    if (parse_body_expression (names, method, &index_expr)) {
        return 1;
    }
    
    if (expect_token (TOK_RBRACK, "']' after array index")) {
    
        free_field_mul_expr (&index_expr);
        return 1;
    
    }
    
    get_token ();
    
    if (index_expr.has_field && index_expr.field && index_expr.field[0] == '$' && (!index_expr.factors || strcmp (index_expr.factors, "1") == 0)) {
        index_local = strtoul (index_expr.field + 1, 0, 10);
    } else if (add_constructor_argument_local (names, method, &index_expr, &index_local)) {
    
        free_field_mul_expr (&index_expr);
        return 1;
    
    }
    
    sprintf (temp_name, "__j2d_array_get_%lu", method->nb_locals);
    
    if (add_local_info (method, temp_name, array_type + 1, result_local_out)) {
    
        free_field_mul_expr (&index_expr);
        return 1;
    
    }
    
    sprintf (index_source, "$%lu", index_local);
    add_local_statement (method, *result_local_out, ARG_ARRAY_GET, array_source, index_source);
    
    free_field_mul_expr (&index_expr);
    return 0;

}

static int parse_string_concat_argument (struct source_names *names, struct method_info *method, char **text_out, int *dynamic_out, struct concat_field_ref **refs_out, unsigned long *nb_refs_out) {

    char *folded = 0;
    char *concat = 0;
    
    unsigned long folded_len = 0;
    unsigned long concat_len = 0;
    
    int need_operand = 1;
    int dynamic = 0;
    int parenthesized = 0;
    
    *text_out = 0;
    *dynamic_out = 0;
    
    *refs_out = 0;
    *nb_refs_out = 0;
    
    while (tok.kind != TOK_RPAREN) {
    
        if (!need_operand) {
        
            if (tok.kind != TOK_PLUS) {
                break;
            }
            
            need_operand = 1;
            
            get_token ();
            continue;
        
        }
        
        if (tok.kind == TOK_PPSTR) {
        
            char *part = copy_string_literal (tok.ident);
            
            append_text (&folded, &folded_len, part);
            concat_append_literal (&concat, &concat_len, part);
            
            free (part);
            
            get_token ();
            need_operand = 0;
            
            continue;
        
        }
        
        parenthesized = 0;
        
        if (tok.kind == TOK_LPAREN) {
        
            struct field_mul_expr expr;
            unsigned long local_index;
            
            char local_name[64];
            char local_type[2];
            
            const char *literal;
            char value[64];
            
            if (parse_body_expression (names, method, &expr)) {
            
                free (folded);
                free (concat);
                free_concat_field_refs (*refs_out, *nb_refs_out);
                
                *refs_out = 0;
                *nb_refs_out = 0;
                
                return 1;
            
            }
            
            local_type[0] = expr.type ? expr.type : 'I';
            local_type[1] = '\0';
            
            sprintf (local_name, "__j2d_concat_%lu", method->nb_locals);
            
            if (add_local_info (method, local_name, local_type, &local_index)) {
            
                free_field_mul_expr (&expr);
                free (folded);
                free (concat);
                free_concat_field_refs (*refs_out, *nb_refs_out);
                
                *refs_out = 0;
                *nb_refs_out = 0;
                return 1;
            
            }
            
            if (!expr.has_field) {
            
                literal = expr.literal_text;
                
                if (!literal) {
                
                    sprintf (value, "%ld", expr.constant);
                    literal = value;
                
                }
                
                add_local_statement (method, local_index, ARG_INT, 0, literal);
            
            } else {
            
                if (!expr.factors) {
                    expr.factors = xstrdup ("1");
                }
                
                add_local_statement (method, local_index, ARG_ARITHMETIC, expr.field, expr.factors);
            
            }
            
            concat_append_local (&concat, &concat_len, local_index);
            
            dynamic = 1;
            need_operand = 0;
            
            free_field_mul_expr (&expr);
            continue;
        
        }
        
        if (tok.kind == TOK_THIS) {
        
            get_token ();
            
            if (expect_token (TOK_DOT, "'.' after this")) {
            
                free (folded);
                free (concat);
                
                free_concat_field_refs (*refs_out, *nb_refs_out);
                *refs_out = 0; *nb_refs_out = 0;
                
                return 1;
            
            }
            
            get_token ();
            
            if (expect_ident ("field name after 'this.'")) {
            
                free (folded);
                free (concat);
                
                free_concat_field_refs (*refs_out, *nb_refs_out);
                *refs_out = 0; *nb_refs_out = 0;
                
                return 1;
            
            }
        
        }
        
        if (tok.kind == TOK_IDENT) {
        
            unsigned long local_index;
            unsigned long param_index;
            
            struct field_info *field = find_field_info_by_name (names, tok.ident);
            struct local_info *local = find_local_info_by_name (method, tok.ident, &local_index);
            
            int is_param = !find_param_index (method, tok.ident, &param_index);
            
            if (is_param) {
            
                get_token ();
                
                if (tok.kind == TOK_DOT) {
                
                    char target[64];
                    sprintf (target, "#%lu", param_index);
                    
                    if (parse_concat_instance_call (method, target, &concat, &concat_len)) {
                    
                        free_concat_field_refs (*refs_out, *nb_refs_out);
                        free (folded);
                        free (concat);
                        
                        *refs_out = 0;
                        *nb_refs_out = 0;
                        
                        return 1;
                    
                    }
                
                } else {
                    concat_append_param (&concat, &concat_len, param_index);
                }
                
                need_operand = 0;
                dynamic = 1;
                
                continue;
            
            }
            
            if (local) {
            
                get_token ();
                
                if (local->type[0] == '[' && tok.kind == TOK_LBRACK) {
                
                    char array_source[64];
                    sprintf (array_source, "$%lu", local_index);
                    
                    if (parse_concat_array_get (names, method, array_source, local->type, &local_index)) {
                    
                        free_concat_field_refs (*refs_out, *nb_refs_out);
                        free (folded);
                        free (concat);
                        
                        *refs_out = 0;
                        *nb_refs_out = 0;
                        
                        return 1;
                    
                    }
                
                }
                
                if (tok.kind == TOK_DOT) {
                
                    char target[64];
                    sprintf (target, "$%lu", local_index);
                    
                    if (parse_concat_instance_call (method, target, &concat, &concat_len)) {
                    
                        free_concat_field_refs (*refs_out, *nb_refs_out);
                        free (folded);
                        free (concat);
                        
                        *refs_out = 0;
                        *nb_refs_out = 0;
                        
                        return 1;
                    
                    }
                
                } else {
                    concat_append_local (&concat, &concat_len, local_index);
                }

                dynamic = 1;
                
                if (parenthesized) {
                
                    if (expect_token (TOK_RPAREN, "')' after parenthesized value")) {
                    
                        free_concat_field_refs (*refs_out, *nb_refs_out);
                        
                        free (folded);
                        free (concat);
                        
                        *refs_out = 0;
                        *nb_refs_out = 0;
                        
                        return 1;
                    
                    }
                    
                    get_token ();
                
                }
                
                need_operand = 0;
                continue;
            
            }
            
            if (!field) {
            
                char *class_name = xstrdup (tok.ident);
                get_token ();
                
                if (tok.kind == TOK_DOT) {
                
                    if (parse_concat_static_call (method, class_name, &concat, &concat_len)) {
                    
                        free_concat_field_refs (*refs_out, *nb_refs_out);
                        free (class_name);
                        
                        free (folded);
                        free (concat);
                        
                        *refs_out = 0;
                        *nb_refs_out = 0;
                        
                        return 1;
                    
                    }
                    
                    free (class_name);
                    
                    need_operand = 0;
                    dynamic = 1;
                    
                    continue;
                
                }
                
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown field or local '%s' in string concatenation", class_name);
                free (class_name);
                
                free (folded);
                free (concat);
                
                free_concat_field_refs (*refs_out, *nb_refs_out);
                
                *refs_out = 0;
                *nb_refs_out = 0;
                
                return 1;
            
            }
            
            add_concat_field_ref (refs_out, nb_refs_out, field->name, get_line_number (), tok.start, tok.caret);
            get_token ();
            
            if (field->type[0] == '[' && tok.kind == TOK_LBRACK) {
            
                unsigned long receiver_local;
                char receiver_target[64];
                
                if (parse_concat_array_get (names, method, field->name, field->type, &receiver_local)) {
                
                    free_concat_field_refs (*refs_out, *nb_refs_out);
                    free (folded);
                    free (concat);
                    
                    *refs_out = 0;
                    *nb_refs_out = 0;
                    
                    return 1;
                
                }
                
                sprintf (receiver_target, "$%lu", receiver_local);
                
                if (tok.kind == TOK_DOT) {
                
                    if (parse_concat_instance_call (method, receiver_target, &concat, &concat_len)) {
                    
                        free_concat_field_refs (*refs_out, *nb_refs_out);
                        free (folded);
                        free (concat);
                        
                        *refs_out = 0;
                        *nb_refs_out = 0;
                        
                        return 1;
                    
                    }
                
                } else {
                    concat_append_local (&concat, &concat_len, receiver_local);
                }
                
                need_operand = 0;
                dynamic = 1;
                
                continue;
            
            }
            
            if (tok.kind == TOK_DOT) {
            
                char receiver_name[64];
                char receiver_target[64];
                
                unsigned long receiver_local;
                sprintf (receiver_name, "__j2d_call_receiver_%lu", method->nb_locals);
                
                if (add_local_info (method, receiver_name, field->type, &receiver_local)) {
                
                    free_concat_field_refs (*refs_out, *nb_refs_out);
                    free (folded);
                    free (concat);
                    
                    *refs_out = 0;
                    *nb_refs_out = 0;
                    
                    return 1;
                
                }
                
                add_local_statement (method, receiver_local, ARG_ARITHMETIC, field->name, "");
                sprintf (receiver_target, "$%lu", receiver_local);
                
                if (parse_concat_instance_call (method, receiver_target, &concat, &concat_len)) {
                
                    free_concat_field_refs (*refs_out, *nb_refs_out);
                    free (folded);
                    free (concat);
                    
                    *refs_out = 0;
                    *nb_refs_out = 0;
                    
                    return 1;
                
                }
                
                need_operand = 0;
                dynamic = 1;
                
                continue;
            
            }
            
            if (!(field->flags & METHOD_STATIC) || field_has_prior_assignment (method, field->name) || field_has_prior_lazy_concat_init (method, field->name)) {
            
                concat_append_field (&concat, &concat_len, field->name);
                dynamic = 1;
            
            } else if (field_initializer_is_mul_di (field->init_text)
                || field_initializer_is_mul_field (field->init_text)
                || field_initializer_is_call (field->init_text)) {
            
                append_text (&concat, &concat_len, CONCAT_MUL_FIELD_PREFIX);
                append_text (&concat, &concat_len, field->name);
                append_text (&concat, &concat_len, CONCAT_SEP);
                
                dynamic = 1;
            
            } else {
            
                char value[64];
                
                if (field->has_initializer && field->init_text) {
                    sprintf (value, "%ld", field_initializer_to_long (field->init_text));
                } else {
                    strcpy (value, "0");
                }
                
                append_text (&folded, &folded_len, value);
                concat_append_literal (&concat, &concat_len, value);
            
            }
            
            if (parenthesized) {
            
                if (expect_token (TOK_RPAREN, "')' after parenthesized value")) {
                
                    free_concat_field_refs (*refs_out, *nb_refs_out);
                    
                    free (folded);
                    free (concat);
                    
                    *refs_out = 0;
                    *nb_refs_out = 0;
                    
                    return 1;
                
                }
                
                get_token ();
            
            }
            
            need_operand = 0;
            continue;
        
        }
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected string literal or field name in string concatenation");
        
        free (folded);
        free (concat);
        
        free_concat_field_refs (*refs_out, *nb_refs_out);
        
        *refs_out = 0;
        *nb_refs_out = 0;
        
        return 1;
    
    }
    
    if (need_operand) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected expression after '+'");
        
        free (folded);
        free (concat);
        
        free_concat_field_refs (*refs_out, *nb_refs_out);
        
        *refs_out = 0;
        *nb_refs_out = 0;
        
        return 1;
    
    }
    
    if (dynamic) {
    
        free (folded);
        
        if (!concat) {
            concat = xstrdup ("");
        }
        
        *text_out = concat;
        *dynamic_out = 1;
        
        return 0;
    
    }
    
    free (concat);
    
    if (!folded) {
        folded = xstrdup ("");
    }
    
    *text_out = folded;
    return 0;

}

static int add_local_info (struct method_info *method, const char *name, const char *type, unsigned long *index_out) {

    struct local_info *local;
    
    if (find_local_info_by_name (method, name, 0)) {
        return 1;
    }
    
    method->locals = xrealloc (method->locals, (method->nb_locals + 1) * sizeof (*method->locals));
    local = &method->locals[method->nb_locals];
    
    local->name = xstrdup (name);
    local->type = xstrdup (type);
    
    local->scope_depth = method->local_scope_depth;
    local->active = 1;
    
    if (index_out) {
        *index_out = method->nb_locals;
    }
    
    method->nb_locals++;
    return 0;

}

static void add_local_statement (struct method_info *method, unsigned long local_index, int arg_kind, const char *source, const char *operations) {

    struct statement_info *stmt;
    char target[64];
    
    method->statements = xrealloc (method->statements, (method->nb_statements + 1) * sizeof (*method->statements));
    stmt = &method->statements[method->nb_statements++];
    
    memset (stmt, 0, sizeof (*stmt));
    sprintf (target, "$%lu", local_index);
    
    stmt->kind = STMT_LOCAL;
    stmt->target = xstrdup (target);
    stmt->arg_kind = arg_kind;
    stmt->method = source ? xstrdup (source) : 0;
    stmt->arg_text = operations ? xstrdup (operations) : 0;

}

static int add_static_field_local (struct source_names *names, struct method_info *method, const char *target, unsigned long *local_index_out) {

    char local_name[64], *owner_desc;
    struct field_info *field = find_static_field_info_by_target (names, target, &owner_desc);
    
    if (!field) {
        return 0;
    }
    
    sprintf (local_name, "__j2d_static_field_%lu", method->nb_locals);
    
    if (add_local_info (method, local_name, field->type, local_index_out)) {
    
        free (owner_desc);
        return -1;
    
    }
    
    add_local_statement (method, *local_index_out, ARG_STATIC_GET, owner_desc, field->name);
    
    free (owner_desc);
    return 1;

}

static void add_parameter_statement (struct method_info *method, unsigned long param_index, int arg_kind, const char *source, const char *operations) {

    struct statement_info *stmt;
    char target[64];
    
    method->statements = xrealloc (method->statements, (method->nb_statements + 1) * sizeof (*method->statements));
    stmt = &method->statements[method->nb_statements++];
    
    memset (stmt, 0, sizeof (*stmt));
    sprintf (target, "#%lu", param_index);
    
    stmt->kind = STMT_LOCAL;
    stmt->target = xstrdup (target);
    stmt->arg_kind = arg_kind;
    stmt->method = source ? xstrdup (source) : 0;
    stmt->arg_text = operations ? xstrdup (operations) : 0;

}

static void add_field_statement (struct method_info *method, const char *field_name, int arg_kind, const char *source, const char *operations) {

    struct statement_info *stmt;
    
    method->statements = xrealloc (method->statements, (method->nb_statements + 1) * sizeof (*method->statements));
    stmt = &method->statements[method->nb_statements++];
    
    memset (stmt, 0, sizeof (*stmt));
    
    stmt->kind = STMT_LOCAL;
    stmt->target = xstrdup (field_name);
    stmt->arg_kind = arg_kind;
    stmt->method = source ? xstrdup (source) : 0;
    stmt->arg_text = operations ? xstrdup (operations) : 0;

}

static int parse_body_expression (struct source_names *names, struct method_info *method, struct field_mul_expr *expr);
static void free_field_mul_expr (struct field_mul_expr *expr);

static int merge_field_expression (struct field_mul_expr *left, struct field_mul_expr *right, int operation);
static void append_field_mul_operation (struct field_mul_expr *expr, int operation, long value);

static const char *method_param_type (struct method_info *method, unsigned long param_index);

static int parse_local_assignment_after_name (struct source_names *names, struct method_info *method, unsigned long local_index, int operation) {

    struct field_mul_expr expr;
    const char *local_type = method->locals[local_index].type;
    
    if (!operation && tok.kind == TOK_PPSTR && (local_type[0] == 'L' || local_type[0] == '[')) {
    
        char *value = copy_string_literal (tok.ident);
        get_token ();
        
        if (expect_token (TOK_SEMI, "';' after assignment")) {
        
            free (value);
            return 1;
        
        }
        
        add_local_statement (method, local_index, ARG_STRING, 0, value);
        
        free (value);
        return 0;
    
    }
    
    if (operation) {
    
        char source[64];
        struct field_mul_expr right;
        
        sprintf (source, "$%lu", local_index);
        memset (&expr, 0, sizeof (expr));
        
        expr.field = xstrdup (source);
        expr.type = local_type[0];
        expr.has_field = 1;
        
        if (parse_body_expression (names, method, &right)) {
        
            free_field_mul_expr (&expr);
            return 1;
        
        }
        
        if (merge_field_expression (&expr, &right, operation)) {
        
            free_field_mul_expr (&right);
            free_field_mul_expr (&expr);
            
            return 1;
        
        }
        
        free_field_mul_expr (&right);
    
    } else if (parse_body_expression (names, method, &expr)) {
        return 1;
    }
    
    if (expect_token (TOK_SEMI, "';' after assignment")) {
    
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if ((local_type[0] == 'Z' && expr.type != 'Z') || (local_type[0] != 'Z' && expr.type == 'Z')) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "incompatible types in boolean assignment");
    
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if ((local_type[0] == 'I' || local_type[0] == 'S' || local_type[0] == 'J'
        || local_type[0] == 'B' || local_type[0] == 'C')
            && (expr.type == 'D' || expr.type == 'F')) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "incompatible types: possible lossy conversion in assignment");
        
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if (!operation && expr.has_field && expr.type != local_type[0]
        && primitive_widening_assignable (expr.type, local_type[0])) {
    
        char conversion[3];
        
        conversion[0] = '!';
        conversion[1] = local_type[0];
        conversion[2] = '\0';
        
        if (expr.factors_len) {
            append_text (&expr.factors, &expr.factors_len, "*");
        }
        
        append_text (&expr.factors, &expr.factors_len, conversion);
        expr.type = local_type[0];
    
    }
    
    if (!expr.has_field) {
    
        const char *literal = expr.literal_text;
        char value[64];
        
        if (!literal) {
        
            sprintf (value, "%ld", expr.constant);
            literal = value;
        
        }
        
        add_local_statement (method, local_index, ARG_INT, 0, literal);
    
    } else {
    
        if (!expr.factors) {
            expr.factors = xstrdup ("1");
        }
        
        add_local_statement (method, local_index, ARG_ARITHMETIC, expr.field, expr.factors);
    
    }
    
    free_field_mul_expr (&expr);
    return 0;

}

static int parse_parameter_assignment_after_name (struct source_names *names, struct method_info *method, unsigned long param_index, int operation) {

    struct field_mul_expr expr;
    const char *param_type = method_param_type (method, param_index);
    
    if (!operation && tok.kind == TOK_PPSTR && (param_type[0] == 'L' || param_type[0] == '[')) {
    
        char *value = copy_string_literal (tok.ident);
        get_token ();
        
        if (expect_token (TOK_SEMI, "';' after assignment")) {
        
            free (value);
            return 1;
        
        }
        
        add_parameter_statement (method, param_index, ARG_STRING, 0, value);
        
        free (value);
        return 0;
    
    }
    
    if (operation) {
    
        char source[64];
        struct field_mul_expr right;
        
        sprintf (source, "#%lu", param_index);
        memset (&expr, 0, sizeof (expr));
        
        expr.field = xstrdup (source);
        expr.type = param_type[0];
        expr.has_field = 1;
        
        if (parse_body_expression (names, method, &right)) {
        
            free_field_mul_expr (&expr);
            return 1;
        
        }
        
        if (merge_field_expression (&expr, &right, operation)) {
        
            free_field_mul_expr (&right);
            free_field_mul_expr (&expr);
            
            return 1;
        
        }
        
        free_field_mul_expr (&right);
    
    } else if (parse_body_expression (names, method, &expr)) {
        return 1;
    }
    
    if (expect_token (TOK_SEMI, "';' after assignment")) {
    
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if ((param_type[0] == 'Z' && expr.type != 'Z') || (param_type[0] != 'Z' && expr.type == 'Z')) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "incompatible types in boolean assignment");
        
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if ((param_type[0] == 'I' || param_type[0] == 'S' || param_type[0] == 'J'
        || param_type[0] == 'B' || param_type[0] == 'C')
            && (expr.type == 'D' || expr.type == 'F')) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "incompatible types: possible lossy conversion in assignment");
        
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if (!operation && expr.has_field && expr.type != param_type[0] && primitive_widening_assignable (expr.type, param_type[0])) {
    
        char conversion[3];
        
        conversion[0] = '!';
        conversion[1] = param_type[0];
        conversion[2] = '\0';
        
        if (expr.factors_len) {
            append_text (&expr.factors, &expr.factors_len, "*");
        }
        
        append_text (&expr.factors, &expr.factors_len, conversion);
        expr.type = param_type[0];
    
    }
    
    if (!expr.has_field) {
    
        const char *literal = expr.literal_text;
        char value[64];
        
        if (!literal) {
        
            sprintf (value, "%ld", expr.constant);
            literal = value;
        
        }
        
        add_parameter_statement (method, param_index, ARG_INT, 0, literal);
    
    } else {
    
        if (!expr.factors) {
            expr.factors = xstrdup ("1");
        }
        
        add_parameter_statement (method, param_index, ARG_ARITHMETIC, expr.field, expr.factors);
    
    }
    
    free_field_mul_expr (&expr);
    return 0;

}

static int parse_call_arguments (struct source_names *names, struct method_info *method, char **encoded_out, const char *target_type, const char *called_name);
static int parse_own_call_arguments (struct source_names *names, struct method_info *method, char **encoded_out, const char *called_name);
static int parse_target_call_arguments (struct source_names *names, struct method_info *method, char **encoded_out, const char *target, const char *called_name);

static int parse_field_assignment_after_name (struct source_names *names, struct method_info *method, struct field_info *field, int operation) {

    struct field_mul_expr expr;
    const char *field_type = field->type;
    
    if (!operation && tok.kind == TOK_NEW) {
    
        char *new_name;
        char *new_type;
        
        char *ctor_args = 0;
        char local_name[64];
        
        char source[64];
        unsigned long local_index;
        
        const char *init_filename = xstrdup (get_filename ());
        const char *init_start = xstrdup (tok.start);
        const char *init_caret = init_start + (tok.caret - tok.start);
        
        unsigned long init_line = get_line_number ();
        get_token ();
        
        if (tok.kind == TOK_BYTE || tok.kind == TOK_CHAR
            || tok.kind == TOK_SHORT || tok.kind == TOK_INT
                || tok.kind == TOK_LONG || tok.kind == TOK_FLOAT
                    || tok.kind == TOK_DOUBLE || tok.kind == TOK_BOOLEAN) {
        
            char *array_type;
            char size_source[64];
            char array_source[64];
            char array_name[64];
            
            unsigned long size_local;
            unsigned long array_local;
            
            struct field_mul_expr size_expr;
            
            switch (tok.kind) {
            
                case TOK_BYTE:
                
                    new_type = xstrdup ("[B");
                    break;
                
                case TOK_CHAR:
                
                    new_type = xstrdup ("[C");
                    break;
                
                case TOK_SHORT:
                
                    new_type = xstrdup ("[S");
                    break;
                
                case TOK_INT:
                
                    new_type = xstrdup ("[I");
                    break;
                
                case TOK_LONG:
                
                    new_type = xstrdup ("[J");
                    break;
                
                case TOK_FLOAT:
                
                    new_type = xstrdup ("[F");
                    break;
                
                case TOK_DOUBLE:
                
                    new_type = xstrdup ("[D");
                    break;
                
                default:
                
                    new_type = xstrdup ("[Z");
                    break;
            
            }
            
            get_token ();
            
            if (expect_token (TOK_LBRACK, "'[' in array allocation")) {
            
                free (new_type);
                return 1;
            
            }
            
            get_token ();
            
            if (parse_body_expression (names, method, &size_expr)) {
            
                free (new_type);
                return 1;
            
            }
            
            if (expect_token (TOK_RBRACK, "']' after array size")) {
            
                free_field_mul_expr (&size_expr);
                free (new_type);
                
                return 1;
            
            }
            
            get_token ();
            
            if (expect_token (TOK_SEMI, "';' after array assignment")) {
            
                free_field_mul_expr (&size_expr);
                free (new_type);
                
                return 1;
            
            }
            
            array_type = new_type;
            
            if (strcmp (field_type, array_type) != 0) {
            
                report_line_at (get_filename (), init_line, REPORT_ERROR, init_start, init_caret, "incompatible array type in assignment");
                
                free_field_mul_expr (&size_expr);
                free (array_type);
                
                return 1;
            
            }
            
            sprintf (array_name, "__j2d_new_field_%lu", method->nb_locals);
            
            if (add_constructor_argument_local (names, method, &size_expr, &size_local)
                || add_local_info (method, array_name, array_type, &array_local)) {
            
                free_field_mul_expr (&size_expr);
                free (array_type);
                
                return 1;
            
            }
            
            sprintf (size_source, "$%lu", size_local);
            add_local_statement (method, array_local, ARG_NEW_ARRAY, size_source, array_type);
            
            sprintf (array_source, "$%lu", array_local);
            add_field_statement (method, field->name, ARG_ARITHMETIC, array_source, "");
            
            free_field_mul_expr (&size_expr);
            free (array_type);
            
            return 0;
        
        }
        
        if (expect_ident ("class name after 'new'")) {
            return 1;
        }
        
        new_name = xstrdup (tok.ident);
        new_type = resolve_class_type (names, new_name);
        
        get_token ();
        
        if (expect_token (TOK_LPAREN, "'(' after class name")) {
        
            free (new_type);
            free (new_name);
            
            return 1;
        
        }
        
        get_token ();
        
        if (parse_call_arguments (names, method, &ctor_args, new_type, "<init>")) {
        
            free (new_type);
            free (new_name);
            
            return 1;
        
        }
        
        get_token ();
        
        if (expect_token (TOK_SEMI, "';' after assignment")) {
        
            free (ctor_args);
            free (new_type);
            free (new_name);
            
            return 1;
        
        }
        
        if (strcmp (field_type, new_type) != 0) {
        
            report_line_at (get_filename (), init_line, REPORT_ERROR, init_start, init_caret, "incompatible types in object assignment");
            
            free (ctor_args);
            free (new_type);
            free (new_name);
            
            return 1;
        
        }
        
        sprintf (local_name, "__j2d_new_field_%lu", method->nb_locals);
        
        if (add_local_info (method, local_name, field_type, &local_index)) {
        
            free (ctor_args);
            free (new_type);
            free (new_name);
            
            return 1;
        
        }
        
        add_local_statement (method, local_index, ARG_NEW, new_name, ctor_args);
        
        method->statements[method->nb_statements - 1].filename = init_filename;
        method->statements[method->nb_statements - 1].line = init_line;
        method->statements[method->nb_statements - 1].start = init_start;
        method->statements[method->nb_statements - 1].caret = init_caret;
        
        sprintf (source, "$%lu", local_index);
        add_field_statement (method, field->name, ARG_ARITHMETIC, source, "");
        
        free (ctor_args);
        free (new_type);
        free (new_name);
        
        return 0;
    
    }
    
    if (!operation && tok.kind == TOK_PPSTR && (field_type[0] == 'L' || field_type[0] == '[')) {
    
        char *value = copy_string_literal (tok.ident);
        get_token ();
        
        if (expect_token (TOK_SEMI, "';' after assignment")) {
        
            free (value);
            return 1;
        
        }
        
        add_field_statement (method, field->name, ARG_STRING, 0, value);
        
        free (value);
        return 0;
    
    }
    
    if (operation) {
    
        struct field_mul_expr right;
        memset (&expr, 0, sizeof (expr));
        
        expr.field = xstrdup (field->name);
        expr.type = field_type[0];
        expr.has_field = 1;
        
        if (parse_body_expression (names, method, &right)) {
        
            free_field_mul_expr (&expr);
            return 1;
        
        }
        
        if (merge_field_expression (&expr, &right, operation)) {
        
            free_field_mul_expr (&right);
            free_field_mul_expr (&expr);
            
            return 1;
        
        }
        
        free_field_mul_expr (&right);
    
    } else if (parse_body_expression (names, method, &expr)) {
        return 1;
    }
    
    if (expect_token (TOK_SEMI, "';' after assignment")) {
    
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if ((field_type[0] == 'Z' && expr.type != 'Z') || (field_type[0] != 'Z' && expr.type == 'Z')) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "incompatible types in boolean assignment");
        
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if ((field_type[0] == 'I' || field_type[0] == 'S' || field_type[0] == 'J'
        || field_type[0] == 'B' || field_type[0] == 'C')
            && (expr.type == 'D' || expr.type == 'F')) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "incompatible types: possible lossy conversion in assignment");
        
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if (!operation && expr.has_field && expr.type != field_type[0] && primitive_widening_assignable (expr.type, field_type[0])) {
    
        char conversion[3];
        
        conversion[0] = '!';
        conversion[1] = field_type[0];
        conversion[2] = '\0';
        
        if (expr.factors_len) {
            append_text (&expr.factors, &expr.factors_len, "*");
        }
        
        append_text (&expr.factors, &expr.factors_len, conversion);
        expr.type = field_type[0];
    
    }
    
    if (!expr.has_field) {
    
        const char *literal = expr.literal_text;
        char value[64];
        
        if (!literal) {
        
            sprintf (value, "%ld", expr.constant);
            literal = value;
        
        }
        
        add_field_statement (method, field->name, ARG_INT, 0, literal);
    
    } else {
    
        if (!expr.factors) {
            expr.factors = xstrdup ((field_type[0] == 'L' || field_type[0] == '[') ? "" : "1");
        }
        
        add_field_statement (method, field->name, ARG_ARITHMETIC, expr.field, expr.factors);
    
    }
    
    free_field_mul_expr (&expr);
    return 0;

}

static const char *method_param_type (struct method_info *method, unsigned long param_index);

static int class_target_available (struct source_names *all_names, unsigned long nb_sources, struct source_names *names, const char *target);
static int declared_reference_assignable (const char *actual, const char *formal);

static int method_declares_throw (struct method_info *method, const char *type);
static int parse_call_arguments (struct source_names *names, struct method_info *method, char **encoded_out, const char *target_type, const char *called_name);
static int parse_own_call_arguments (struct source_names *names, struct method_info *method, char **encoded_out, const char *called_name);
static int parse_target_call_arguments (struct source_names *names, struct method_info *method, char **encoded_out, const char *target, const char *called_name);

static int parse_throw_statement (struct source_names *names, struct method_info *method) {

    struct statement_info *stmt;
    struct local_info *local;
    
    unsigned long index;
    const char *type;
    const char *start;
    const char *caret;
    
    unsigned long line;
    char target[32];
    
    (void) names;
    
    start = xstrdup (tok.start);
    caret = start + (tok.caret - tok.start);
    
    line = get_line_number ();
    get_token ();
    
    if (tok.kind == TOK_NEW) {
    
        char *new_name;
        char *new_type;
        
        char *ctor_args = 0;
        char local_name[64];
        
        unsigned long constructor_caret_offset;
        unsigned long local_index;
        
        get_token ();
        
        if (expect_ident ("class name after 'new'")) {
        
            free ((char *) start);
            return 1;
        
        }
        
        new_name = xstrdup (tok.ident);
        
        if (!class_target_available (declaration_names, nb_declaration_names, names, new_name)) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "unknown class '%s'", new_name);
            
            free (new_name);
            free ((char *) start);
            
            return 1;
        
        }
        
        new_type = resolve_class_type (names, new_name);
        
        constructor_caret_offset = (unsigned long) (tok.caret - tok.start);
        get_token ();
        
        if (expect_token (TOK_LPAREN, "'(' after class name")) {
        
            free (new_type);
            free (new_name);
            
            free ((char *) start);
            return 1;
        
        }
        
        get_token ();
        
        if (parse_call_arguments (names, method, &ctor_args, new_type, "<init>")) {
        
            free (new_type);
            free (new_name);
            
            free ((char *) start);
            return 1;
        
        }
        
        get_token ();
        
        if (expect_token (TOK_SEMI, "';' after throw statement")) {
        
            free (ctor_args);
            
            free (new_type);
            free (new_name);
            
            free ((char *) start);
            return 1;
        
        }
        
        sprintf (local_name, "__j2d_throw_%lu", method->nb_locals);
        
        if (add_local_info (method, local_name, new_type, &local_index)) {
        
            free (ctor_args);
            
            free (new_type);
            free (new_name);
            
            free ((char *) start);
            return 1;
        
        }
        
        add_local_statement (method, local_index, ARG_NEW, new_name, ctor_args);
        
        method->statements[method->nb_statements - 1].filename = xstrdup (get_filename ());
        method->statements[method->nb_statements - 1].line = line;
        method->statements[method->nb_statements - 1].start = xstrdup (start);
        method->statements[method->nb_statements - 1].caret = method->statements[method->nb_statements - 1].start + constructor_caret_offset;
        
        sprintf (target, "$%lu", local_index);
        
        if (!method->try_depth && !declared_reference_assignable (new_type, JAVA_RUNTIME_EXCEPTION_TYPE)
            && !declared_reference_assignable (new_type, JAVA_ERROR_TYPE) && !method_declares_throw (method, new_type)) {
        
            report_line_at (get_filename (), line, REPORT_ERROR, start, caret, "unreported thrown type %s; must be declared in method throws clause", new_name);
            
            free (ctor_args);
            free (new_type);
            free (new_name);
            
            free ((char *) start);
            return 1;
        
        }
        
        free (ctor_args);
        free (new_type);
        free (new_name);
        
    } else if (tok.kind != TOK_IDENT) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "expected exception expression after 'throw'");
        
        free ((char *) start);
        return 1;
    
    }
    
    if (tok.kind == TOK_IDENT) {
    
        local = find_local_info_by_name (method, tok.ident, &index);
        
        if (local) {
        
            type = local->type;
            sprintf (target, "$%lu", index);
        
        } else if (!find_param_index (method, tok.ident, &index)) {
        
            type = method_param_type (method, index);
            sprintf (target, "#%lu", index);
        
        } else {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "unknown variable '%s'", tok.ident);
            
            free ((char *) start);
            return 1;
        
        }
        
        if (!type || (type[0] != 'L' && type[0] != '[')) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "thrown value must be an object reference");
            
            free ((char *) start);
            return 1;
        
        }
        
        get_token ();
        
        if (expect_token (TOK_SEMI, "';' after throw statement")) {
        
            free ((char *) start);
            return 1;
        
        }
    
    }

    method->statements = xrealloc (method->statements, (method->nb_statements + 1) * sizeof (*method->statements));
    stmt = &method->statements[method->nb_statements++];
    
    memset (stmt, 0, sizeof (*stmt));
    
    stmt->kind = STMT_THROW;
    stmt->target = xstrdup (target);
    
    stmt->filename = xstrdup (get_filename ());
    stmt->line = line;
    
    stmt->start = start;
    stmt->caret = caret;
    
    return 0;

}

static struct statement_info *append_control_statement (struct method_info *method, int kind);

static int class_target_available (struct source_names *all_names, unsigned long nb_sources, struct source_names *names, const char *target);
static int statement_terminator = TOK_SEMI;

static int parse_statement (struct source_names *names, struct method_info *method) {

    const char *target_start = 0;
    const char *target_caret = 0;
    
    char *target = 0;
    char *name = 0;
    
    const char *call_start;
    const char *call_caret;
    
    int this_member = 0;
    unsigned long call_line;
    
    unsigned long target_line = 0;
    
    if (tok.kind == TOK_INCR || tok.kind == TOK_DECR) {
    
        struct field_mul_expr expr;
        
        if (parse_body_expression (names, method, &expr)) {
            return 1;
        }
        
        free_field_mul_expr (&expr);
        
        if (expect_token (statement_terminator, "';' after increment or decrement")) {
            return 1;
        }
        
        return 0;
    
    }
    
    if (tok.kind == TOK_THIS) {
    
        char *encoded_args = 0;
        
        const char *this_start;
        const char *this_caret;
        
        unsigned long this_line;
        
        this_caret = ((this_start = xstrdup (tok.start)) + (tok.caret - tok.start));
        this_line = get_line_number ();
        
        get_token ();
        
        if (tok.kind == TOK_LPAREN) {
        
            get_token ();
            
            if (tok.kind != TOK_RPAREN && parse_own_call_arguments (names, method, &encoded_args, "<init>")) {
            
                free ((char *) this_start);
                return 1;
            
            }
            
            if (expect_token (TOK_RPAREN, "')' after this constructor call")) {
            
                free (encoded_args);
                
                free ((char *) this_start);
                return 1;
            
            }
            
            get_token ();
            
            if (expect_token (statement_terminator, "';' after this constructor call")) {
            
                free (encoded_args);
                
                free ((char *) this_start);
                return 1;
            
            }
            
            add_call_statement (method, "@this", "<init>",
                encoded_args ? ARG_MULTI : ARG_NONE, encoded_args, 0,
                    this_line, this_start, this_caret, 0, 0);
            free (encoded_args);
            
            return 0;
        
        }
        
        if (expect_token (TOK_DOT, "'.' or '(' after this")) {
        
            free ((char *) this_start);
            return 1;
        
        }
        
        get_token ();
        
        if (expect_ident ("field or method name after 'this.'")) {
        
            free ((char *) this_start);
            return 1;
        
        }
        
        call_start = this_start;
        call_caret = this_caret;
        call_line = this_line;
        
        name = xstrdup (tok.ident);
        
        get_token ();
        this_member = 1;
        
        goto have_statement_name;
    
    }
    
    if (tok.kind == TOK_SUPER) {
    
        char *encoded_args = 0;
        
        const char *super_start;
        const char *super_caret;
        
        unsigned long super_line;
        char *super_method;
        
        super_caret = ((super_start = xstrdup (tok.start)) + (tok.caret - tok.start));
        super_line = get_line_number ();
        
        get_token ();
        
        if (tok.kind == TOK_DOT) {
        
            get_token ();
            
            if (expect_ident ("method name after 'super.'")) {
            
                free ((char *) super_start);
                return 1;
            
            }
            
            super_method = xstrdup (tok.ident);
            get_token ();
        
        } else {
            super_method = xstrdup ("<init>");
        }
        
        if (expect_token (TOK_LPAREN, "'(' after super")) {
        
            free (super_method);
            
            free ((char *) super_start);
            return 1;
        
        }
        
        get_token ();
        
        if (tok.kind != TOK_RPAREN && parse_call_arguments (names, method, &encoded_args, names->super_class, super_method)) {
        
            free (super_method);
            
            free ((char *) super_start);
            return 1;
        
        }
        
        if (expect_token (TOK_RPAREN, "')' after super call")) {
        
            free (encoded_args);
            free (super_method);
            
            free ((char *) super_start);
            return 1;
        
        }
        
        get_token ();
        
        if (expect_token (statement_terminator, "';' after super call")) {
        
            free (encoded_args);
            free (super_method);
            
            free ((char *) super_start);
            return 1;
        
        }
        
        add_call_statement (method, "@super", super_method,
            encoded_args ? ARG_MULTI : ARG_NONE, encoded_args, 0,
                super_line, super_start, super_caret, 0, 0);
        
        free (encoded_args);
        free (super_method);
        
        return 0;
    
    }
    
    if (tok.kind != TOK_IDENT) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected statement");
        return 1;
    
    }
    
    call_caret = ((call_start = xstrdup (tok.start)) + (tok.caret - tok.start));
    call_line = get_line_number ();
    
    name = xstrdup (tok.ident);
    get_token ();
    
have_statement_name:
    
    if (tok.kind == TOK_COLON) {
    
        struct statement_info *stmt;
        unsigned long i;
        
        for (i = 0; i < method->nb_statements; i++) {
        
            if (method->statements[i].kind == STMT_LABEL && method->statements[i].target && strcmp (method->statements[i].target, name) == 0) {
            
                report_line_at (get_filename (), call_line, REPORT_ERROR, call_start, call_caret, "duplicate label '%s'", name);
                
                free ((char *) call_start);
                free (name);
                
                return 1;
            
            }
        
        }
        
        stmt = append_control_statement (method, STMT_LABEL);
        stmt->target = name;
        
        stmt->filename = xstrdup (get_filename ());
        stmt->line = call_line;
        
        stmt->start = call_start;
        stmt->caret = call_caret;
        
        return 0;
    
    }
    
    if (tok.kind == TOK_LBRACK) {
    
        unsigned long array_local;
        unsigned long index_local;
        unsigned long value_local;
            
        struct local_info *array_info;
        struct field_mul_expr index_expr;
        struct field_mul_expr value_expr;
        
        array_info = find_local_info_by_name (method, name, &array_local);
        
        if (!array_info) {
        
            struct field_info *array_field = find_field_info_by_name (names, name);
            
            if (array_field && array_field->type[0] == '[') {
            
                char array_name[64];
                
                if ((method->flags & METHOD_STATIC) && !(array_field->flags & METHOD_STATIC)) {
                
                    report_line_at (get_filename (), call_line, REPORT_ERROR, call_start, call_caret,
                        "non-static field '%s' cannot be referenced from a static method", name);
                    
                    free ((char *) call_start);
                    free (name);
                    
                    return 1;
                
                }
                
                sprintf (array_name, "__j2d_array_field_%lu", method->nb_locals);
                
                if (add_local_info (method, array_name, array_field->type, &array_local)) {
                
                    free ((char *) call_start);
                    free (name);
                    
                    return 1;
                
                }
                
                add_local_statement (method, array_local, ARG_ARITHMETIC, array_field->name, "");
                array_info = &method->locals[array_local];
            
            }
        
        }
        
        if (!array_info || array_info->type[0] != '[') {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, call_start, call_caret, "'%s' is not an array", name);
            
            free ((char *) call_start);
            free (name);
            
            return 1;
        
        }
        
        get_token ();
        
        if (parse_body_expression (names, method, &index_expr)) {
        
            free ((char *) call_start);
            free (name);
            
            return 1;
        
        }
        
        if (expect_token (TOK_RBRACK, "']' after array index")) {
        
            free_field_mul_expr (&index_expr);
            free ((char *) call_start);
            free (name);
            
            return 1;
        
        }
        
        get_token ();
        
        if (tok.kind == TOK_INCR || tok.kind == TOK_DECR) {
        
            const char *element_type = array_info->type + 1;
            int operation = tok.kind == TOK_INCR ? TOK_PLUS : TOK_MINUS;
            
            char value_name[64], array_source[64], index_source[64], value_source[64];
            struct field_mul_expr update;
            
            if (element_type[0] != 'B' && element_type[0] != 'C' && element_type[0] != 'S'
                && element_type[0] != 'I' && element_type[0] != 'J'
                    && element_type[0] != 'F' && element_type[0] != 'D') {
            
                report_line_at (get_filename (), call_line, REPORT_ERROR, call_start, call_caret,
                    "increment or decrement requires a numeric array element");
                
                free_field_mul_expr (&index_expr);
                free ((char *) call_start);
                free (name);
                
                return 1;
            
            }
            
            get_token ();
            
            if (expect_token (statement_terminator, "';' after array increment or decrement")) {
            
                free_field_mul_expr (&index_expr);
                free ((char *) call_start);
                free (name);
                
                return 1;
            
            }
            
            if (add_constructor_argument_local (names, method, &index_expr, &index_local)) {
            
                free_field_mul_expr (&index_expr);
                free ((char *) call_start);
                free (name);
                
                return 1;
            
            }
            
            sprintf (value_name, "__j2d_array_incdec_%lu", method->nb_locals);
            
            if (add_local_info (method, value_name, element_type, &value_local)) {
            
                free_field_mul_expr (&index_expr);
                free ((char *) call_start);
                free (name);
                
                return 1;
            
            }
            
            sprintf (array_source, "$%lu", array_local);
            sprintf (index_source, "$%lu", index_local);
            sprintf (value_source, "$%lu", value_local);
            
            add_local_statement (method, value_local, ARG_ARRAY_GET, array_source, index_source);
            
            memset (&update, 0, sizeof (update));
            update.field = xstrdup (value_source);
            update.type = element_type[0];
            update.has_field = 1;
            
            append_field_mul_operation (&update, operation, 1);
            
            add_local_statement (method, value_local, ARG_ARITHMETIC, update.field, update.factors);
            add_array_put_statement (method, array_local, index_local, value_local);
            
            free_field_mul_expr (&update);
            free_field_mul_expr (&index_expr);
            
            free ((char *) call_start);
            free (name);
            
            return 0;
        
        }
        
        if (expect_token (TOK_ASSIGN, "'=' after array element")) {
        
            free_field_mul_expr (&index_expr);
            free ((char *) call_start);
            free (name);
            
            return 1;
        
        }
        
        if (add_constructor_argument_local (names, method, &index_expr, &index_local)) {
        
            free_field_mul_expr (&index_expr);
            free ((char *) call_start);
            free (name);
            
            return 1;
        
        }
        
        get_token ();
        
        if (parse_body_expression (names, method, &value_expr)) {
        
            free_field_mul_expr (&index_expr);
            free ((char *) call_start);
            free (name);
            
            return 1;
        
        }
        
        if (expect_token (statement_terminator, "';' after array assignment")) {
        
            free_field_mul_expr (&value_expr);
            free_field_mul_expr (&index_expr);
            
            free ((char *) call_start);
            free (name);
            
            return 1;
        
        }
        
        if (add_constructor_argument_local (names, method, &value_expr, &value_local)) {
        
            free_field_mul_expr (&value_expr);
            free_field_mul_expr (&index_expr);
            
            free ((char *) call_start);
            free (name);
            
            return 1;
        
        }
        
        add_array_put_statement (method, array_local, index_local, value_local);
        
        free_field_mul_expr (&value_expr);
        free_field_mul_expr (&index_expr);
        
        free ((char *) call_start);
        free (name);
        
        return 0;
    
    }
    
    if (tok.kind == TOK_ASSIGN || tok.kind == TOK_PLUS || tok.kind == TOK_MINUS
        || tok.kind == TOK_STAR || tok.kind == TOK_BSLASH || tok.kind == TOK_MOD
            || tok.kind == TOK_AMPER || tok.kind == TOK_CARET || tok.kind == TOK_PIPE
                || tok.kind == TOK_LSH || tok.kind == TOK_RSH || tok.kind == TOK_URSH
                    || tok.kind == TOK_PLUSEQ || tok.kind == TOK_MINUSEQ
                        || tok.kind == TOK_STAREQ || tok.kind == TOK_SLASHEQ
                            || tok.kind == TOK_MODEQ || tok.kind == TOK_ANDEQ
                                || tok.kind == TOK_XOREQ || tok.kind == TOK_OREQ
                                        || tok.kind == TOK_LSHEQ || tok.kind == TOK_RSHEQ || tok.kind == TOK_URSHEQ
                                            || tok.kind == TOK_INCR || tok.kind == TOK_DECR) {
    
        unsigned long local_index = 0;
        unsigned long param_index = 0;
        
        int combined_assignment = 0;
        int operation = 0;
        
        struct local_info *local = this_member ? 0 : find_local_info_by_name (method, name, &local_index);
        struct field_info *field = find_field_info_by_name (names, name);
        
        int is_param = !this_member && !find_param_index (method, name, &param_index);
        
        if (!local && !field && !is_param) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                call_start, call_caret, "unknown variable '%s'", name);
            
            free ((char *) call_start);
            free (name);
            
            return 1;
        
        }
        
        if (tok.kind != TOK_ASSIGN) {
        
            operation = tok.kind;
            
            if (operation == TOK_PLUSEQ) {
            
                operation = TOK_PLUS;
                combined_assignment = 1;
            
            } else if (operation == TOK_MINUSEQ) {
            
                operation = TOK_MINUS;
                combined_assignment = 1;
            
            } else if (operation == TOK_STAREQ) {
            
                operation = TOK_STAR;
                combined_assignment = 1;
            
            } else if (operation == TOK_SLASHEQ) {
            
                operation = TOK_BSLASH;
                combined_assignment = 1;
            
            } else if (operation == TOK_MODEQ) {
            
                operation = TOK_MOD;
                combined_assignment = 1;
            
            } else if (operation == TOK_ANDEQ) {
            
                operation = TOK_AMPER;
                combined_assignment = 1;
            
            } else if (operation == TOK_XOREQ) {
            
                operation = TOK_CARET;
                combined_assignment = 1;
            
            } else if (operation == TOK_OREQ) {
            
                operation = TOK_PIPE;
                combined_assignment = 1;
            
            } else if (operation == TOK_LSHEQ) {
            
                operation = TOK_LSH;
                combined_assignment = 1;
            
            } else if (operation == TOK_RSHEQ) {
            
                operation = TOK_RSH;
                combined_assignment = 1;
            
            } else if (operation == TOK_URSHEQ) {
            
                operation = TOK_URSH;
                combined_assignment = 1;
            
            } else if (operation == TOK_INCR || operation == TOK_DECR) {
            
                int increment = operation == TOK_INCR;
                
                operation = increment ? TOK_PLUS : TOK_MINUS;
                combined_assignment = 1;
                
                get_token ();
                
                if (expect_token (statement_terminator, "';' after increment or decrement")) {
                
                    free ((char *) call_start);
                    free (name);
                    
                    return 1;
                
                }
                
                if (local) {
                
                    struct field_mul_expr expr;
                    char source[64];
                    
                    memset (&expr, 0, sizeof (expr));
                    sprintf (source, "$%lu", local_index);
                    
                    expr.field = xstrdup (source);
                    expr.type = local->type[0];
                    expr.has_field = 1;
                    
                    append_field_mul_operation (&expr, operation, 1);
                    add_local_statement (method, local_index, ARG_ARITHMETIC, expr.field, expr.factors);
                    
                    free_field_mul_expr (&expr);
                
                } else if (is_param) {
                
                    struct field_mul_expr expr;
                    char source[64];
                    
                    memset (&expr, 0, sizeof (expr));
                    sprintf (source, "#%lu", param_index);
                    
                    expr.field = xstrdup (source);
                    expr.type = method_param_type (method, param_index)[0];
                    expr.has_field = 1;
                    
                    append_field_mul_operation (&expr, operation, 1);
                    add_parameter_statement (method, param_index, ARG_ARITHMETIC, expr.field, expr.factors);
                    
                    free_field_mul_expr (&expr);
                
                } else {
                
                    struct field_mul_expr expr;
                    memset (&expr, 0, sizeof (expr));
                    
                    expr.field = xstrdup (field->name);
                    expr.type = field->type[0];
                    expr.has_field = 1;
                    
                    append_field_mul_operation (&expr, operation, 1);
                    add_field_statement (method, field->name, ARG_ARITHMETIC, expr.field, expr.factors);
                    
                    free_field_mul_expr (&expr);
                
                }
                
                free ((char *) call_start);
                free (name);
                
                return 0;
            
            }
            
            get_token ();
            
            if (!combined_assignment) {
            
                if (expect_token (TOK_ASSIGN, "'=' after compound assignment operator")) {
                
                    free ((char *) call_start);
                    free (name);
                    
                    return 1;
                
                }
                
                get_token ();
            
            }
        
        } else {
            get_token ();
        }
        
        if (local) {
        
            free ((char *) call_start);
            free (name);
            
            return parse_local_assignment_after_name (names, method, local_index, operation);
        
        }
        
        if (is_param) {
        
            free ((char *) call_start);
            free (name);
            
            return parse_parameter_assignment_after_name (names, method, param_index, operation);
        
        }
        
        free ((char *) call_start);
        free (name);
        
        return parse_field_assignment_after_name (names, method, field, operation);
    
    }
    
    while (tok.kind == TOK_DOT) {
    
        char *old_target;
        unsigned long len;
        
        if (!target_start) {
        
            target_start = xstrdup (call_start);
            
            target_caret = target_start + (call_caret - call_start);
            target_line = call_line;
        
        }
        
        get_token ();
        
        if (expect_ident ("method or field name")) {
        
            free ((char *) target_start);
            
            free (target);
            free (name);
            
            return 1;
        
        }
        
        old_target = target;
        
        if (old_target) {
        
            len = strlen (old_target) + 1 + strlen (name);
            
            target = xmalloc (len + 1);
            sprintf (target, "%s.%s", old_target, name);
            
            free (old_target);
            free (name);
        
        } else {
            target = name;
        }
        
        free ((char *) call_start);
        
        call_caret = ((call_start = xstrdup (tok.start)) + (tok.caret - tok.start));
        call_line = get_line_number ();
        
        name = xstrdup (tok.ident);
        get_token ();
    
    }
    
    if (target) {
    
        unsigned long local_index = 0;
        
        struct local_info *local = find_local_info_by_name (method, target, &local_index);
        struct field_info *field = find_field_info_by_name (names, target);
        
        unsigned long target_param_index;
        int is_param = !find_param_index (method, target, &target_param_index);
        
        if (field && !local && !is_param && !(field->flags & METHOD_STATIC) && (method->flags & METHOD_STATIC)) {
        
            report_line_at (get_filename (), target_line, REPORT_ERROR, target_start, target_caret, "non-static field '%s' cannot be referenced from a static method", target);
            
            free ((char *) target_start);
            free ((char *) call_start);
            
            free (target);
            free (name);
            
            return 1;
        
        }
        
        if (local) {
        
            char encoded[64];
            sprintf (encoded, "$%lu", local_index);
            
            free (target);
            target = xstrdup (encoded);
        
        } else if (is_param) {
        
            const char *param_type = method_param_type (method, target_param_index);
            unsigned long receiver_local;
            
            char local_name[64], encoded[64], source[64];
            
            if (!param_type || (param_type[0] != 'L' && param_type[0] != '[')) {
            
                report_line_at (get_filename (), target_line, REPORT_ERROR, target_start, target_caret,
                    "cannot invoke method on non-object parameter '%s'", target);
                
                free ((char *) target_start);
                free ((char *) call_start);
                
                free (target);
                free (name);
                
                return 1;
            
            }
            
            sprintf (local_name, "__j2d_call_param_%lu", method->nb_locals);
            
            {
            
                char *receiver_type = copy_first_type_descriptor (param_type);
                int failed = add_local_info (method, local_name, receiver_type, &receiver_local);
                
                free (receiver_type);
                
                if (failed) {
                
                    free ((char *) target_start);
                    free ((char *) call_start);
                    
                    free (target);
                    free (name);
                    
                    return 1;
                
                }
            
            }
            
            sprintf (source, "#%lu", target_param_index);
            add_local_statement (method, receiver_local, ARG_ARITHMETIC, source, "1");
            
            sprintf (encoded, "$%lu", receiver_local);
            free (target);
            
            target = xstrdup (encoded);
        
        } else if (field && !is_param) {
        
            char encoded[64];
            char local_name[64];
            
            unsigned long field_local;
            
            if (field->type[0] != 'L' && field->type[0] != '[') {
            
                report_line_at (get_filename (), target_line, REPORT_ERROR, target_start, target_caret, "cannot invoke method on non-object field '%s'", target);
                
                free ((char *) target_start);
                free ((char *) call_start);
                
                free (target);
                free (name);
                
                return 1;
            
            }
            
            sprintf (local_name, "__j2d_call_field_%lu", method->nb_locals);
            
            if (add_local_info (method, local_name, field->type, &field_local)) {
            
                free ((char *) call_start);
                free (target);
                free (name);
                
                return 1;
            
            }
            
            add_local_statement (method, field_local, ARG_ARITHMETIC, field->name, "");
            
            sprintf (encoded, "$%lu", field_local);
            free (target);
            
            target = xstrdup (encoded);
        
        } else if (!field && !is_param && strchr (target, '.')) {
        
            unsigned long field_local;
            int found = add_static_field_local (names, method, target, &field_local);
            
            if (found < 0) {
            
                free ((char *) target_start);
                free ((char *) call_start);
                
                free (target);
                free (name);
                
                return 1;
            
            }
            
            if (found) {
            
                char encoded[64];
                
                sprintf (encoded, "$%lu", field_local);
                free (target);
                
                target = xstrdup (encoded);
            
            } else if (!class_target_available (declaration_names, nb_declaration_names, names, target)) {
            
                report_line_at (get_filename (), target_line, REPORT_ERROR, target_start, target_caret, "unknown class '%s'", target);
                
                free ((char *) target_start);
                free ((char *) call_start);
                
                free (target);
                free (name);
                
                return 1;
            
            }
        
        } else if (!field && !is_param && target[0] >= 'a' && target[0] <= 'z') {
        
            report_line_at (get_filename (), target_line, REPORT_ERROR, target_start, target_caret, "unknown variable '%s'", target);
            
            free ((char *) target_start);
            free ((char *) call_start);
            
            free (target);
            free (name);
            
            return 1;
        
        } else if (!field && !is_param && !class_target_available (declaration_names, nb_declaration_names, names, target)) {
        
            report_line_at (get_filename (), target_line, REPORT_ERROR, target_start, target_caret, "unknown class '%s'", target);
            
            free ((char *) target_start);
            free ((char *) call_start);
            
            free (target);
            free (name);
            
            return 1;
        
        }
    
    }
    
    free ((char *) target_start);
    
    if (expect_token (TOK_LPAREN, "'('") ) {
    
        free (target);
        free (name);
        
        return 1;
    
    }
    
    get_token ();
    
    if (tok.kind == TOK_RPAREN) {
        add_call_statement (method, target, name, ARG_NONE, 0, 0, call_line, call_start, call_caret, 0, 0);
    } else {
    
        char *encoded_args = 0;
        
        if (parse_target_call_arguments (names, method, &encoded_args, target, name)) {
        
            free (target);
            free (name);
            
            return 1;
        
        }
        
        add_call_statement (method, target, name, ARG_MULTI, encoded_args, 0, call_line, call_start, call_caret, 0, 0);
        free (encoded_args);
    
    }
    
    if (expect_token (TOK_RPAREN, "')'") ) {
    
        free (target);
        free (name);
        
        return 1;
    
    }
    
    get_token ();
    
    if (expect_token (statement_terminator, "';'") ) {
    
        free (target);
        free (name);
        
        return 1;
    
    }
    
    free (target);
    free (name);
    
    return 0;

}

static double field_initializer_numeric_value (const char *text);

static int parse_body_expression (struct source_names *names, struct method_info *method, struct field_mul_expr *expr);
static int parse_body_unary (struct source_names *names, struct method_info *method, struct field_mul_expr *expr);

static void free_field_mul_expr (struct field_mul_expr *expr);

static int make_incdec_expression (struct method_info *method, struct local_info *local, unsigned long local_index, struct field_info *field, int operation, int postfix, struct field_mul_expr *expr) {

    const char *type;
    char source[64];
    struct field_mul_expr update;
    
    if (!local && !field) {
        return 1;
    }
    
    type = local ? local->type : field->type;

    if (type[0] != 'B' && type[0] != 'C' && type[0] != 'S' && type[0] != 'I' && type[0] != 'J' && type[0] != 'F' && type[0] != 'D') {
        return 1;
    }
    
    if (local) {
        sprintf (source, "$%lu", local_index);
    } else {
        strcpy (source, field->name);
    }
    
    memset (expr, 0, sizeof (*expr));
    
    if (postfix) {
    
        unsigned long temp_index;
        char temp_name[64];
        char temp_source[64];
        
        sprintf (temp_name, "__j2d_incdec_%lu", method->nb_locals);
        
        if (add_local_info (method, temp_name, type, &temp_index)) {
            return 1;
        }
        
        add_local_statement (method, temp_index, ARG_ARITHMETIC, source, "1");
        sprintf (temp_source, "$%lu", temp_index);
        
        expr->field = xstrdup (temp_source);
        expr->type = type[0];
        expr->has_field = 1;
    
    }
    
    memset (&update, 0, sizeof (update));
    
    update.field = xstrdup (source);
    update.type = type[0];
    update.has_field = 1;
    
    append_field_mul_operation (&update, operation, 1);
    
    if (local) {
        add_local_statement (method, local_index, ARG_ARITHMETIC, update.field, update.factors);
    } else {
        add_field_statement (method, field->name, ARG_ARITHMETIC, update.field, update.factors);
    }
    
    free_field_mul_expr (&update);
    
    if (!postfix) {
    
        expr->field = xstrdup (source);
        expr->type = type[0];
        expr->has_field = 1;
    
    }
    
    return 0;

}

static char *copy_first_type_descriptor (const char *type) {

    const char *end = type;
    char *copy;
    
    while (*end == '[') {
        end++;
    }
    
    if (*end == 'L') {
    
        while (*end && *end != ';') {
            end++;
        }
        
        if (*end == ';') {
            end++;
        }
    
    } else if (*end) {
        end++;
    }
    
    copy = xmalloc ((unsigned long) (end - type) + 1);
    memcpy (copy, type, (size_t) (end - type));
    
    copy[end - type] = '\0';
    return copy;

}

static int split_constructor_arguments (const char *text, char **params_out, unsigned long **locals_out, unsigned long *nb_locals_out);

static int parse_body_instance_call (struct source_names *names, struct method_info *method, unsigned long receiver_local, const char *receiver_type, struct field_mul_expr *expr) {

    const char *call_start;
    const char *call_caret;
    const char *return_type = 0;
    
    unsigned long call_line;
    unsigned long result_local;
    
    char *encoded_args = 0;
    char *params = 0;
    
    unsigned long *arg_locals = 0, nb_arg_locals = 0;
    
    char *method_name;
    char *class_type;
    char receiver_source[64];
    char result_name[64];
    char result_source[64];
    
    unsigned long i;
    get_token ();
    
    if (expect_ident ("method name after '.'")) {
        return 1;
    }
    
    call_start = xstrdup (tok.start);
    call_caret = call_start + (tok.caret - tok.start);
    call_line = get_line_number ();
    
    method_name = xstrdup (tok.ident);
    get_token ();
    
    if (expect_token (TOK_LPAREN, "'(' after instance method name")) {
    
        free ((char *) call_start);
        free (method_name);
        
        return 1;
    
    }
    
    get_token ();
    
    if (parse_call_arguments (names, method, &encoded_args, receiver_type, method_name)) {
    
        free ((char *) call_start);
        free (method_name);
        
        return 1;
    
    }
    
    if (expect_token (TOK_RPAREN, "')' after instance method arguments")) {
    
        free ((char *) call_start);
        free (encoded_args);
        free (method_name);
        
        return 1;
    
    }
    
    get_token ();
    
    if (split_constructor_arguments (encoded_args, &params, &arg_locals, &nb_arg_locals)) {
    
        free ((char *) call_start);
        free (encoded_args);
        free (method_name);
        
        return 1;
    
    }
    
    /* Parsing arguments may reallocate the local-variable table. */
    receiver_type = method->locals[receiver_local].type;
    class_type = dex_class_descriptor (names);
    
    if (strcmp (receiver_type, class_type) == 0) {
    
        for (i = 0; i < names->nb_methods; i++) {
        
            struct method_info *callee = &names->methods[i];
            
            if (!(callee->flags & METHOD_STATIC) && strcmp (callee->java_name, method_name) == 0 && strcmp (callee->params, params) == 0) {
            
                return_type = callee->return_type;
                break;
            
            }
        
        }
    
    }
    
    free (class_type);
    
    if (!return_type) {
        return_type = lookup_declared_method_return_type (receiver_type, method_name, params, 0);
    }
    
    if (!return_type || return_type[0] == 'V') {
    
        report_line_at (get_filename (), call_line, REPORT_ERROR, call_start, call_caret,
            "unknown return type for instance method call '%s'", method_name);
        
        free ((char *) call_start);
        free (encoded_args);
        free (method_name);
        free (arg_locals);
        free (params);
        
        return 1;
    
    }
    
    sprintf (result_name, "__j2d_call_result_%lu", method->nb_locals);
    
    if (add_local_info (method, result_name, return_type, &result_local)) {
    
        free ((char *) call_start);
        free (encoded_args);
        free (method_name);
        free (arg_locals);
        free (params);
        
        return 1;
    
    }
    
    sprintf (receiver_source, "$%lu", receiver_local);
    sprintf (result_source, "$%lu", result_local);
    
    if (nb_arg_locals) {
    
        add_call_statement (method, receiver_source, method_name, ARG_RESULT_MULTI, encoded_args,
            result_local, call_line, call_start, call_caret, 0, 0);
    
    } else {
    
        add_call_statement (method, receiver_source, method_name, ARG_RESULT_CALL, result_source,
            0, call_line, call_start, call_caret, 0, 0);
    
    }
    
    free_field_mul_expr (expr);
    
    expr->field = xstrdup (result_source);
    expr->type = return_type[0];
    expr->has_field = 1;
    
    free (encoded_args);
    free (method_name);
    free (arg_locals);
    free (params);
    
    if (tok.kind == TOK_DOT && method->locals[result_local].type[0] == 'L') {
        return parse_body_instance_call (names, method, result_local, method->locals[result_local].type, expr);
    }
    
    return 0;

}

static int parse_array_element_assignment (struct source_names *names, struct method_info *method, unsigned long array_local, unsigned long index_local, const char *element_type, struct field_mul_expr *expr) {

    struct field_mul_expr value;
    
    const char *actual_type;
    unsigned long value_local, converted_local;
    
    char local_name[64], source[64], conversion[3];
    int compatible;
    
    get_token ();
    
    if (parse_body_expression (names, method, &value)) {
        return 1;
    }
    
    actual_type = expression_argument_type (names, method, &value);
    compatible = 0;
    
    if (value.type == 'N' && (element_type[0] == 'L' || element_type[0] == '[')) {
        compatible = 1;
    } else if (actual_type && (element_type[0] == 'L' || element_type[0] == '[')) {
        compatible = declared_reference_assignable (actual_type, element_type);
    } else if (actual_type) {
        compatible = primitive_widening_assignable (actual_type[0], element_type[0]);
    }
    
    if (!compatible) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "incompatible types in array element assignment");
        
        free_field_mul_expr (&value);
        return 1;
    
    }
    
    if (value.type == 'N') {
    
        sprintf (local_name, "__j2d_array_assign_%lu", method->nb_locals);
        
        if (add_local_info (method, local_name, element_type, &value_local)) {
        
            free_field_mul_expr (&value);
            return 1;
        
        }
        
        add_local_statement (method, value_local, ARG_INT, 0, "0");
    
    } else if (add_constructor_argument_local (names, method, &value, &value_local)) {
    
        free_field_mul_expr (&value);
        return 1;
    
    }
    
    if (strcmp (method->locals[value_local].type, element_type) != 0) {
    
        sprintf (local_name, "__j2d_array_assign_%lu", method->nb_locals);
        
        if (add_local_info (method, local_name, element_type, &converted_local)) {
        
            free_field_mul_expr (&value);
            return 1;
        
        }
        
        sprintf (source, "$%lu", value_local);
        
        conversion[0] = '!';
        conversion[1] = element_type[0];
        conversion[2] = '\0';
        
        add_local_statement (method, converted_local, ARG_ARITHMETIC, source,
            element_type[0] == 'L' || element_type[0] == '[' ? "" : conversion);
        value_local = converted_local;
    
    }
    
    add_array_put_statement (method, array_local, index_local, value_local);
    sprintf (source, "$%lu", value_local);
    
    free_field_mul_expr (expr);
    
    expr->field = xstrdup (source);
    expr->type = element_type[0];
    expr->has_field = 1;
    
    free_field_mul_expr (&value);
    return 0;

}

static int parse_body_primary (struct source_names *names, struct method_info *method, struct field_mul_expr *expr) {

    unsigned long param_index;
    int this_member = 0;
    
    memset (expr, 0, sizeof (*expr));
    
    if (tok.kind == TOK_NEW) {
    
        char *new_name;
        char *new_type;
        
        char local_name[64];
        char source[64];
        
        char *encoded_args = 0;
        unsigned long local_index;
        
        get_token ();
        
        if (tok.kind == TOK_BYTE || tok.kind == TOK_CHAR
            || tok.kind == TOK_SHORT || tok.kind == TOK_INT
                || tok.kind == TOK_LONG || tok.kind == TOK_FLOAT
                    || tok.kind == TOK_DOUBLE || tok.kind == TOK_BOOLEAN) {
        
            char array_type[3];
            char size_source[64];
            
            struct field_mul_expr size_expr;
            unsigned long size_local;
            
            array_type[0] = '[';
            array_type[1] = type_descriptor (tok.kind)[0];
            array_type[2] = '\0';
            
            get_token ();
            
            if (expect_token (TOK_LBRACK, "'[' in array allocation")) {
                return 1;
            }
            
            get_token ();
            
            if (parse_body_expression (names, method, &size_expr)) {
                return 1;
            }
            
            if (expect_token (TOK_RBRACK, "']' after array size")) {
            
                free_field_mul_expr (&size_expr);
                return 1;
            
            }
            
            get_token ();
            
            if (add_constructor_argument_local (names, method, &size_expr, &size_local)) {
            
                free_field_mul_expr (&size_expr);
                return 1;
            
            }
            
            sprintf (local_name, "__j2d_new_result_%lu", method->nb_locals);
            
            if (add_local_info (method, local_name, array_type, &local_index)) {
            
                free_field_mul_expr (&size_expr);
                return 1;
            
            }
            
            sprintf (size_source, "$%lu", size_local);
            add_local_statement (method, local_index, ARG_NEW_ARRAY, size_source, array_type);
            
            sprintf (source, "$%lu", local_index);
            
            expr->field = xstrdup (source);
            expr->type = '[';
            expr->has_field = 1;
            
            free_field_mul_expr (&size_expr);
            return 0;
        
        }
        
        if (expect_ident ("class name after 'new'")) {
            return 1;
        }
        
        if (!class_target_available (declaration_names, nb_declaration_names, names, tok.ident)) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "unknown class '%s'", tok.ident);
            
            return 1;
        
        }
        
        new_name = xstrdup (tok.ident);
        new_type = resolve_class_type (names, new_name);
        
        get_token ();
        
        if (expect_token (TOK_LPAREN, "'(' after class name")) {
        
            free (new_type);
            free (new_name);
            
            return 1;
        
        }
        
        get_token ();
        
        if (parse_call_arguments (names, method, &encoded_args, new_type, "<init>")) {
        
            free (new_type);
            free (new_name);
            
            return 1;
        
        }
        
        if (expect_token (TOK_RPAREN, "')' after constructor arguments")) {
        
            free (encoded_args);
            
            free (new_type);
            free (new_name);
            
            return 1;
        
        }
        
        get_token ();
        sprintf (local_name, "__j2d_new_result_%lu", method->nb_locals);
        
        if (add_local_info (method, local_name, new_type, &local_index)) {
        
            free (encoded_args);
            
            free (new_type);
            free (new_name);
            
            return 1;
        
        }
        
        add_local_statement (method, local_index, ARG_NEW, new_name, encoded_args);
        sprintf (source, "$%lu", local_index);
        
        expr->field = xstrdup (source);
        expr->type = 'L';
        expr->has_field = 1;
        
        free (encoded_args);
        
        free (new_type);
        free (new_name);
        
        return 0;
    
    }
    
    if (tok.kind == TOK_LPAREN) {
    
        get_token ();
        
        if (tok.kind == TOK_BYTE || tok.kind == TOK_CHAR || tok.kind == TOK_SHORT
            || tok.kind == TOK_INT || tok.kind == TOK_LONG || tok.kind == TOK_FLOAT
                || tok.kind == TOK_DOUBLE) {
        
            char *cast_desc = 0;
            
            if (parse_type_name (names, 0, &cast_desc)) {
                return 1;
            }
            
            if (expect_token (TOK_RPAREN, "')' after cast type")) {
            
                free (cast_desc);
                return 1;
            
            }
            
            get_token ();
            
            if (tok.kind == TOK_LPAREN) {
            
                get_token ();
                
                if (parse_body_expression (names, method, expr)) {
                
                    free (cast_desc);
                    return 1;
                
                }
                
                if (expect_token (TOK_RPAREN, "')' after cast expression")) {
                
                    free_field_mul_expr (expr);
                    free (cast_desc);
                    
                    return 1;
                
                }
                
                get_token ();
            
            } else if (parse_body_unary (names, method, expr)) {
            
                free (cast_desc);
                return 1;
            
            }
            
            if (expr->has_field) {
            
                if (expr->factors_len) {
                    append_text (&expr->factors, &expr->factors_len, "*");
                }
                
                append_text (&expr->factors, &expr->factors_len, "!");
                append_text (&expr->factors, &expr->factors_len, cast_desc);
                
                expr->type = cast_desc[0];
            
            } else if (expr->is_constant) {
                expr->type = cast_desc[0];
            }
            
            free (cast_desc);
            return 0;
        
        }
        
        if (tok.kind == TOK_IDENT && peek_token_kind () == TOK_RPAREN
            && class_target_available (declaration_names, nb_declaration_names, names, tok.ident)) {
        
            char *cast_desc = 0;
            unsigned long source_local, result_local;
            
            char local_name[64], source[64], result[64];
            
            if (parse_type_name (names, 0, &cast_desc)) {
                return 1;
            }
            
            if (expect_token (TOK_RPAREN, "')' after cast type")) {
            
                free (cast_desc);
                return 1;
            
            }
            
            get_token ();
            
            if (parse_body_unary (names, method, expr)) {
            
                free (cast_desc);
                return 1;
            
            }
            
            if ((expr->type != 'L' && expr->type != '[') || !expr->has_field
                || add_constructor_argument_local (names, method, expr, &source_local)) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "reference cast requires a reference expression");
                
                free_field_mul_expr (expr);
                free (cast_desc);
                
                return 1;
            
            }
            
            sprintf (local_name, "__j2d_cast_%lu", method->nb_locals);
            
            if (add_local_info (method, local_name, cast_desc, &result_local)) {
            
                free_field_mul_expr (expr);
                free (cast_desc);
                
                return 1;
            
            }
            
            sprintf (source, "$%lu", source_local);
            add_local_statement (method, result_local, ARG_CHECKCAST, source, cast_desc);
            
            sprintf (result, "$%lu", result_local);
            free_field_mul_expr (expr);
            
            expr->field = xstrdup (result);
            expr->type = cast_desc[0];
            expr->has_field = 1;
            
            free (cast_desc);
            return 0;
        
        }
        
        if (parse_body_expression (names, method, expr)) {
            return 1;
        }
        
        if (expect_token (TOK_RPAREN, "')' after parenthesized expression")) {
        
            free_field_mul_expr (expr);
            return 1;
        
        }
        
        get_token ();
        
        /**
         * Preserve grouping before an outer operator is appended to the
         * linear arithmetic factor stream.
         */
        if (expr->has_field && expr->factors_len) {
        
            unsigned long grouped_local;
            char grouped_source[64];
            
            if (add_constructor_argument_local (names, method, expr, &grouped_local)) {
            
                free_field_mul_expr (expr);
                return 1;
            
            }
            
            free_field_mul_expr (expr);
            sprintf (grouped_source, "$%lu", grouped_local);
            
            expr->field = xstrdup (grouped_source);
            expr->type = method->locals[grouped_local].type[0];
            expr->has_field = 1;
        
        }
        
        if (tok.kind == TOK_DOT && expr->has_field && expr->field
            && expr->field[0] == '$' && (expr->type == 'L' || expr->type == '[')) {
        
            unsigned long result_local = strtoul (expr->field + 1, 0, 10);
            return parse_body_instance_call (names, method, result_local, method->locals[result_local].type, expr);
        
        }
        
        return 0;
    
    }
    
    if (tok.kind == TOK_THIS) {
    
        char *this_type;
        unsigned long this_local;
        
        char this_name[64];
        char source[64];
        
        if (method->flags & METHOD_STATIC) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "cannot use 'this' in a static method");
            return 1;
        
        }
        
        this_member = 1;
        get_token ();
        
        if (tok.kind != TOK_DOT) {
        
            this_type = dex_class_descriptor (names);
            sprintf (this_name, "__j2d_this_%lu", method->nb_locals);
            
            if (add_local_info (method, this_name, this_type, &this_local)) {
            
                free (this_type);
                return 1;
            
            }
            
            add_local_statement (method, this_local, ARG_THIS, 0, 0);
            sprintf (source, "$%lu", this_local);
            
            expr->field = xstrdup (source);
            expr->type = 'L';
            expr->has_field = 1;
            
            free (this_type);
            return 0;
        
        }
        
        if (expect_token (TOK_DOT, "'.' after this")) {
            return 1;
        }
        
        get_token ();
        
        if (expect_ident ("field name after 'this.'")) {
            return 1;
        }
    
    }
    
    if (tok.kind == TOK_IDENT) {
    
        unsigned long local_index;
        
        struct field_info *field = find_field_info_by_name (names, tok.ident);
        struct local_info *local = this_member ? 0 : find_local_info_by_name (method, tok.ident, &local_index);
        
        if (local) {
        
            char tmp[64];
            sprintf (tmp, "$%lu", local_index);
            
            expr->field = xstrdup (tmp);
            expr->type = local->type[0];
            expr->has_field = 1;
            
            get_token ();
            
            if (local->type[0] == '[' && tok.kind == TOK_LBRACK) {

                struct field_mul_expr index_expr;
                
                unsigned long index_local;
                unsigned long result_local;
                
                char array_source[64];
                char index_source[64];
                char result_source[64];
                char temp_name[64];
                
                const char *element_type = local->type + 1;
                get_token ();
                
                if (parse_body_expression (names, method, &index_expr)) {
                    return 1;
                }
                
                if (expect_token (TOK_RBRACK, "']' after array index")) {
                
                    free_field_mul_expr (&index_expr);
                    return 1;
                
                }
                
                get_token ();
                
                if (add_constructor_argument_local (names, method, &index_expr, &index_local)) {
                
                    free_field_mul_expr (&index_expr);
                    return 1;
                
                }
                
                if (tok.kind == TOK_ASSIGN) {
                
                    int failed = parse_array_element_assignment (names, method,
                        local_index, index_local, element_type, expr);
                    
                    free_field_mul_expr (&index_expr);
                    return failed;
                
                }
                
                sprintf (temp_name, "__j2d_array_get_%lu", method->nb_locals);
                
                if (add_local_info (method, temp_name, element_type, &result_local)) {
                
                    free_field_mul_expr (&index_expr);
                    return 1;
                
                }
                
                sprintf (array_source, "$%lu", local_index);
                sprintf (index_source, "$%lu", index_local);
                
                add_local_statement (method, result_local, ARG_ARRAY_GET, array_source, index_source);
                sprintf (result_source, "$%lu", result_local);
                
                free_field_mul_expr (expr);
                
                expr->field = xstrdup (result_source);
                expr->type = element_type[0];
                expr->has_field = 1;
                
                free_field_mul_expr (&index_expr);
                return 0;
            
            }
            
            if (local->type[0] == '[' && tok.kind == TOK_DOT) {
            
                unsigned long result_local;
                
                char array_source[64];
                char result_source[64];
                char temp_name[64];
                
                get_token ();
                
                if (expect_ident ("'length' after array '.'")) {
                    return 1;
                }
                
                if (strcmp (tok.ident, "length") != 0) {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown array member '%s'", tok.ident);
                    return 1;
                
                }
                
                get_token ();
                
                sprintf (temp_name, "__j2d_array_length_%lu", method->nb_locals);
                
                if (add_local_info (method, temp_name, "I", &result_local)) {
                    return 1;
                }
                
                sprintf (array_source, "$%lu", local_index);
                
                add_local_statement (method, result_local, ARG_ARRAY_LENGTH, array_source, 0);
                sprintf (result_source, "$%lu", result_local);
                
                free_field_mul_expr (expr);
                
                expr->field = xstrdup (result_source);
                expr->type = 'I';
                expr->has_field = 1;
                
                return 0;
            
            }
            
            if (tok.kind == TOK_DOT && local->type[0] == 'L') {
                return parse_body_instance_call (names, method, local_index, local->type, expr);
            }
            
            if (tok.kind == TOK_INCR || tok.kind == TOK_DECR) {
            
                int operation = tok.kind == TOK_INCR ? TOK_PLUS : TOK_MINUS;
                free_field_mul_expr (expr);
                
                get_token ();
                
                return make_incdec_expression (method, local, local_index, 0, operation, 1, expr);
            
            }
            
            return 0;
        
        }
        
        if (!this_member && !find_param_index (method, tok.ident, &param_index)) {
        
            const char *param_type = method_param_type (method, param_index);
            char tmp[64];
            
            const char *parameter_desc = param_type ? param_type : "I";
            unsigned long parameter_local;
            
            char parameter_name[64];
            char parameter_source[64];
            
            sprintf (parameter_name, "__j2d_param_value_%lu", method->nb_locals);
            sprintf (tmp, "#%lu", param_index);
            
            {
            
                char *parameter_type = copy_first_type_descriptor (parameter_desc);
                int failed = add_local_info (method, parameter_name, parameter_type, &parameter_local);
                
                free (parameter_type);
                
                if (failed) {
                    return 1;
                }
            
            }
            
            add_local_statement (method, parameter_local, ARG_ARITHMETIC, tmp, "1");
            sprintf (parameter_source, "$%lu", parameter_local);
            
            expr->type = parameter_desc[0];
            expr->field = xstrdup (parameter_source);
            expr->has_field = 1;
            
            get_token ();
            
            if (param_type && param_type[0] == '[' && tok.kind == TOK_LBRACK) {
            
                struct field_mul_expr index_expr;
                
                unsigned long index_local;
                unsigned long result_local;
                
                char array_source[64];
                char index_source[64];
                char result_source[64];
                char temp_name[64];
                
                const char *element_type = param_type + 1;
                get_token ();
                
                if (parse_body_expression (names, method, &index_expr)) {
                    return 1;
                }
                
                if (expect_token (TOK_RBRACK, "']' after array index")) {
                
                    free_field_mul_expr (&index_expr);
                    return 1;
                
                }
                
                get_token ();
                
                if (add_constructor_argument_local (names, method, &index_expr, &index_local)) {
                
                    free_field_mul_expr (&index_expr);
                    return 1;
                
                }
                
                if (tok.kind == TOK_ASSIGN) {
                
                    int failed = parse_array_element_assignment (names, method,
                        parameter_local, index_local, element_type, expr);
                    
                    free_field_mul_expr (&index_expr);
                    return failed;
                
                }
                
                sprintf (temp_name, "__j2d_array_get_%lu", method->nb_locals);
                
                {
                
                    char *element_desc = copy_first_type_descriptor (element_type);
                    int failed = add_local_info (method, temp_name, element_desc, &result_local);
                    
                    free (element_desc);
                    
                    if (failed) {
                    
                        free_field_mul_expr (&index_expr);
                        return 1;
                    
                    }
                
                }
                
                sprintf (array_source, "#%lu", param_index);
                sprintf (index_source, "$%lu", index_local);
                
                add_local_statement (method, result_local, ARG_ARRAY_GET, array_source, index_source);
                sprintf (result_source, "$%lu", result_local);
                
                free_field_mul_expr (expr);
                
                expr->field = xstrdup (result_source);
                expr->type = element_type[0];
                expr->has_field = 1;
                
                free_field_mul_expr (&index_expr);
                return 0;
            
            }
            
            if (param_type && param_type[0] == '[' && tok.kind == TOK_DOT) {
            
                unsigned long result_local;
                
                char array_source[64];
                char result_source[64];
                char temp_name[64];
                
                get_token ();
                
                if (expect_ident ("'length' after array '.'")) {
                    return 1;
                }
                
                if (strcmp (tok.ident, "length") != 0) {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                        tok.start, tok.caret, "unknown array member '%s'", tok.ident);
                    return 1;
                
                }
                
                get_token ();
                sprintf (temp_name, "__j2d_array_length_%lu", method->nb_locals);
                
                if (add_local_info (method, temp_name, "I", &result_local)) {
                    return 1;
                }
                
                sprintf (array_source, "#%lu", param_index);
                
                add_local_statement (method, result_local, ARG_ARRAY_LENGTH, array_source, 0);
                sprintf (result_source, "$%lu", result_local);
                
                free_field_mul_expr (expr);
                
                expr->field = xstrdup (result_source);
                expr->type = 'I';
                expr->has_field = 1;
                
                return 0;
            
            }
            
            if (param_type && param_type[0] == 'L' && tok.kind == TOK_DOT) {
            
                char *receiver_type = copy_first_type_descriptor (param_type);
                int failed = parse_body_instance_call (names, method, parameter_local, receiver_type, expr);
                
                free (receiver_type);
                return failed;
            
            }
            
            if (tok.kind == TOK_INCR || tok.kind == TOK_DECR) {
            
                struct field_mul_expr update;
                int operation = tok.kind == TOK_INCR ? TOK_PLUS : TOK_MINUS;
                
                char source[64];
                
                if (parameter_desc[0] != 'B' && parameter_desc[0] != 'C' && parameter_desc[0] != 'S'
                    && parameter_desc[0] != 'I' && parameter_desc[0] != 'J'
                        && parameter_desc[0] != 'F' && parameter_desc[0] != 'D') {
                    return 1;
                }
                
                memset (&update, 0, sizeof (update));
                sprintf (source, "#%lu", param_index);
                
                update.field = xstrdup (source);
                update.type = parameter_desc[0];
                update.has_field = 1;
                
                append_field_mul_operation (&update, operation, 1);
                add_parameter_statement (method, param_index, ARG_ARITHMETIC, update.field, update.factors);
                
                free_field_mul_expr (&update);
                get_token ();
            
            }
            
            return 0;
        
        }
        
        if (field) {
        
            if ((method->flags & METHOD_STATIC) && !(field->flags & METHOD_STATIC)) {
            
                report_line_at (get_filename (), get_line_number (),
                    REPORT_ERROR, tok.start, tok.caret,
                        "non-static field '%s' cannot be referenced from a static method",
                            field->name);
                return 1;
            
            }
            
            expr->field = xstrdup (field->name);
            expr->type = field->type[0];
            expr->has_field = 1;
            
            get_token ();
            
            if (field->type[0] == '[' && tok.kind == TOK_LBRACK) {
            
                struct field_mul_expr index_expr;
                
                unsigned long index_local;
                unsigned long result_local;
                
                char index_source[64];
                char result_source[64];
                char temp_name[64];
                
                const char *element_type = field->type + 1;
                get_token ();
                
                if (parse_body_expression (names, method, &index_expr)) {
                    return 1;
                }
                
                if (expect_token (TOK_RBRACK, "']' after array index")) {
                
                    free_field_mul_expr (&index_expr);
                    return 1;
                
                }
                
                get_token ();
                
                if (add_constructor_argument_local (names, method, &index_expr, &index_local)) {
                
                    free_field_mul_expr (&index_expr);
                    return 1;
                
                }
                
                if (tok.kind == TOK_ASSIGN) {
                
                    unsigned long array_local;
                    int failed;
                    
                    sprintf (temp_name, "__j2d_array_field_%lu", method->nb_locals);
                    
                    if (add_local_info (method, temp_name, field->type, &array_local)) {
                    
                        free_field_mul_expr (&index_expr);
                        return 1;
                    
                    }
                    
                    add_local_statement (method, array_local, ARG_ARITHMETIC, field->name, "");
                    failed = parse_array_element_assignment (names, method, array_local, index_local, element_type, expr);
                    
                    free_field_mul_expr (&index_expr);
                    return failed;
                
                }
                
                sprintf (temp_name, "__j2d_array_get_%lu", method->nb_locals);
                
                if (add_local_info (method, temp_name, element_type, &result_local)) {
                
                    free_field_mul_expr (&index_expr);
                    return 1;
                
                }
                
                sprintf (index_source, "$%lu", index_local);
                
                add_local_statement (method, result_local, ARG_ARRAY_GET, field->name, index_source);
                sprintf (result_source, "$%lu", result_local);
                
                free_field_mul_expr (expr);
                
                expr->field = xstrdup (result_source);
                expr->type = element_type[0];
                expr->has_field = 1;
                
                free_field_mul_expr (&index_expr);
                return 0;
            
            }
            
            if (field->type[0] == '[' && tok.kind == TOK_DOT) {
            
                unsigned long result_local;
                
                char result_source[64];
                char temp_name[64];
                
                get_token ();
                
                if (expect_ident ("'length' after array '.'")) {
                    return 1;
                }
                
                if (strcmp (tok.ident, "length") != 0) {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown array member '%s'", tok.ident);
                    return 1;
                
                }
                
                get_token ();
                sprintf (temp_name, "__j2d_array_length_%lu", method->nb_locals);
                
                if (add_local_info (method, temp_name, "I", &result_local)) {
                    return 1;
                }
                
                add_local_statement (method, result_local, ARG_ARRAY_LENGTH, field->name, 0);
                sprintf (result_source, "$%lu", result_local);
                
                free_field_mul_expr (expr);
                
                expr->field = xstrdup (result_source);
                expr->type = 'I';
                expr->has_field = 1;
                
                return 0;
            
            }
            
            if (tok.kind == TOK_DOT && field->type[0] == 'L') {
            
                unsigned long receiver_local;
                
                char receiver_name[64];
                char *receiver_type = xstrdup (field->type);
                
                int result;
                sprintf (receiver_name, "__j2d_call_field_%lu", method->nb_locals);
                
                if (add_local_info (method, receiver_name, receiver_type, &receiver_local)) {
                
                    free (receiver_type);
                    return 1;
                
                }
                
                add_local_statement (method, receiver_local, ARG_ARITHMETIC, field->name, "");
                result = parse_body_instance_call (names, method, receiver_local, receiver_type, expr);
                
                free (receiver_type);
                return result;
            
            }
            
            if (tok.kind == TOK_INCR || tok.kind == TOK_DECR) {

                int operation = tok.kind == TOK_INCR ? TOK_PLUS : TOK_MINUS;
                free_field_mul_expr (expr);
                
                get_token ();
                
                return make_incdec_expression (method, 0, 0, field, operation, 1, expr);
            
            }
            
            return 0;
        
        }
        
        {
        
            const char *target_start = tok.start;
            const char *target_caret = tok.caret;
            
            char *target = xstrdup (tok.ident);
            char *target_name = xstrdup (tok.ident);
            
            unsigned long target_line = get_line_number ();
            unsigned long target_len = strlen (target);
            
            char *called_method = 0;
            get_token ();
            
            if (tok.kind == TOK_LPAREN) {
            
                struct method_info *callee;
                
                char *encoded_args = 0;
                char result_name[64];
                char result_source[64];
                
                unsigned long result_local;
                const char *return_type;
                
                get_token ();
                
                if (parse_own_call_arguments (names, method, &encoded_args, target)) {
                
                    free (target_name);
                    free (target);
                    
                    return 1;
                
                }
                
                get_token ();
                
                 
                {
                
                    const char *sep = strchr (encoded_args, '|');
                    char *params = sep ? xmalloc ((unsigned long) (sep - encoded_args) + 1) : 0;
                    
                    if (params) {
                    
                        memcpy (params, encoded_args, (unsigned long) (sep - encoded_args));
                        params[sep - encoded_args] = '\0';
                    
                    }
                    
                    callee = params ? find_declared_unqualified_method_info (names, target, params) : 0;
                    free (params);
                
                }
                
                if (!callee) {
                
                    report_line_at (get_filename (), target_line, REPORT_ERROR,
                        target_start, target_caret, "unknown method '%s'", target);
                    
                    free (encoded_args);
                    free (target_name);
                    free (target);
                    
                    return 1;
               
                }
                
                if ((method->flags & METHOD_STATIC) && !(callee->flags & METHOD_STATIC)) {
                
                    report_line_at (get_filename (), target_line, REPORT_ERROR,
                        target_start, target_caret, "non-static method '%s' cannot be referenced from a static method", target);
                    
                    free (encoded_args);
                    free (target_name);
                    free (target);
                    
                    return 1;
                
                }
                
                return_type = callee->return_type;
                sprintf (result_name, "__j2d_call_result_%lu", method->nb_locals);
                
                if (add_local_info (method, result_name, return_type, &result_local)) {
                
                    free (encoded_args);
                    free (target_name);
                    free (target);
                    
                    return 1;
                
                }
                
                {
                
                    const char *call_start = xstrdup (target_start);
                    const char *call_caret = call_start + (target_caret - target_start);
                    
                    add_call_statement (method, (callee->flags & METHOD_STATIC) ? names->class_name : 0,
                        target, ARG_RESULT_MULTI, encoded_args, result_local, target_line, call_start, call_caret, 0, 0);
                
                }
                
                sprintf (result_source, "$%lu", result_local);
                
                expr->field = xstrdup (result_source);
                expr->type = return_type[0];
                expr->has_field = 1;
                
                free (encoded_args);
                free (target_name);
                free (target);
                
                if (tok.kind == TOK_DOT && method->locals[result_local].type[0] == 'L') {
                    return parse_body_instance_call (names, method, result_local, method->locals[result_local].type, expr);
                }
                
                return 0;
            
            }
            
            while (tok.kind == TOK_DOT) {
            
                get_token ();
                
                if (expect_ident ("method name in expression")) {
                
                    free (target);
                    free (called_method);
                    
                    return 1;
                
                }
                
                free (called_method);
                
                called_method = xstrdup (tok.ident);
                get_token ();
                
                if (tok.kind != TOK_LPAREN) {
                
                    char *field_target = xmalloc (strlen (target) + strlen (called_method) + 2);
                    
                    unsigned long field_local;
                    int found;
                    
                    sprintf (field_target, "%s.%s", target, called_method);
                    found = add_static_field_local (names, method, field_target, &field_local);
                    
                    free (field_target);
                    
                    if (found < 0) {
                    
                        free (called_method);
                        free (target_name);
                        free (target);
                        
                        return 1;
                    
                    }
                    
                    if (found) {
                    
                        char source[64];
                        sprintf (source, "$%lu", field_local);
                        
                        expr->field = xstrdup (source);
                        expr->type = method->locals[field_local].type[0];
                        expr->has_field = 1;
                        
                        free (called_method);
                        free (target_name);
                        free (target);
                        
                        if (tok.kind == TOK_DOT && expr->type == 'L') {
                            return parse_body_instance_call (names, method, field_local, method->locals[field_local].type, expr);
                        }
                        
                        return 0;
                    
                    }
                
                }
                
                if (tok.kind == TOK_LPAREN) {
                    break;
                }
                
                if (tok.kind != TOK_DOT) {
                
                    report_line_at (get_filename (), target_line, REPORT_ERROR,
                        target_start, target_caret, "unknown variable '%s'", target_name);
                    
                    free (called_method);
                    free (target_name);
                    free (target);
                    
                    return 1;
                
                }
                
                append_text (&target, &target_len, ".");
                append_text (&target, &target_len, called_method);
            
            }
            
            if (!called_method || tok.kind != TOK_LPAREN) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "unknown value '%s' in expression", target);
                
                free (called_method);
                free (target_name);
                free (target);
                
                return 1;
            
            }
            
            if (!class_target_available (declaration_names, nb_declaration_names, names, target)) {
            
                report_line_at (get_filename (), target_line, REPORT_ERROR, target_start, target_caret,
                    "unknown class '%s'", target);
                
                free (called_method);
                free (target_name);
                free (target);
                
                return 1;
            
            }
            
            get_token ();
            
            {
            
                char *encoded_args = 0;
                char *class_desc = resolve_class_type (names, target);
                
                const char *return_type = 0;
                unsigned long result_local;
                
                char result_name[64];
                char result_source[64];
                
                if (parse_call_arguments (names, method, &encoded_args, class_desc, called_method)) {
                
                    free (class_desc);
                    free (called_method);
                    free (target_name);
                    free (target);
                    
                    return 1;
                
                }
                
                if (expect_token (TOK_RPAREN, "')' in expression method call")) {
                
                    free (encoded_args);
                    free (class_desc);
                    free (called_method);
                    free (target_name);
                    free (target);
                    
                    return 1;
                
                }
                
                if (strcmp (target, names->class_name) == 0) {
                
                    const char *sep = strchr (encoded_args, '|');
                    unsigned long i;
                    
                    for (i = 0; sep && i < names->nb_methods; i++) {
                    
                        struct method_info *callee = &names->methods[i];
                        
                        if ((callee->flags & METHOD_STATIC) && strcmp (callee->java_name, called_method) == 0
                            && strlen (callee->params) == (unsigned long) (sep - encoded_args)
                                && strncmp (callee->params, encoded_args, sep - encoded_args) == 0) {
                            return_type = callee->return_type;
                            break;
                        }
                    
                    }
                
                }
                
                if (!return_type) {
                
                    const char *sep = strchr (encoded_args, '|');
                    
                    if (sep) {
                    
                        char *params = xmalloc ((unsigned long) (sep - encoded_args) + 1);
                        
                        memcpy (params, encoded_args, (unsigned long) (sep - encoded_args));
                        params[sep - encoded_args] = '\0';
                        
                        return_type = lookup_declared_method_return_type (class_desc, called_method, params, 1);
                        free (params);
                    
                    }
                
                }
                
                if (!return_type || return_type[0] == 'V') {
                
                    report_line_at (get_filename (), target_line, REPORT_ERROR, target_start, target_caret,
                        "unknown return type for expression method call '%s.%s'", target, called_method);
                    
                    free (encoded_args);
                    free (class_desc);
                    free (called_method);
                    free (target_name);
                    free (target);
                    
                    return 1;
                
                }
                
                sprintf (result_name, "__j2d_call_result_%lu", method->nb_locals);
                
                if (add_local_info (method, result_name, return_type, &result_local)) {
                
                    free (encoded_args);
                    free (class_desc);
                    free (called_method);
                    free (target_name);
                    free (target);
                    
                    return 1;
                
                }
                
                {
                
                    const char *call_start = xstrdup (target_start);
                    const char *call_caret = call_start + (target_caret - target_start);
                    
                    add_call_statement (method, target, called_method, ARG_RESULT_MULTI, encoded_args,
                        result_local, target_line, call_start, call_caret, 0, 0);
                
                }
                
                sprintf (result_source, "$%lu", result_local);
                
                expr->field = xstrdup (result_source);
                expr->type = return_type[0];
                expr->has_field = 1;
                
                get_token ();
                
                free (encoded_args);
                free (class_desc);
                free (called_method);
                free (target_name);
                free (target);
                
                if (tok.kind == TOK_DOT && method->locals[result_local].type[0] == 'L') {
                    return parse_body_instance_call (names, method, result_local, method->locals[result_local].type, expr);
                }
                
                return 0;
            
            }
        
        }
    
    }
    
    if (tok.kind == TOK_PPSTR) {
    
        char local_name[64];
        char source[64];
        
        char *value = copy_string_literal (tok.ident);
        unsigned long local_index;
        
        sprintf (local_name, "__j2d_string_expr_%lu", method->nb_locals);
        
        if (add_local_info (method, local_name, JAVA_STRING_TYPE, &local_index)) {
        
            free (value);
            return 1;
        
        }
        
        add_local_statement (method, local_index, ARG_STRING, 0, value);
        sprintf (source, "$%lu", local_index);
        
        expr->field = xstrdup (source);
        expr->type = 'L';
        expr->has_field = 1;
        
        free (value);
        
        get_token ();
        return 0;
    
    }
    
    if (tok.kind == TOK_TRUE || tok.kind == TOK_FALSE) {
    
        expr->constant = tok.kind == TOK_TRUE;
        expr->type = 'Z';
        expr->is_constant = 1;
        
        get_token ();
        return 0;
    
    }
    
    if (tok.kind == TOK_NULL) {
    
        expr->constant = 0;
        expr->type = 'N';
        expr->is_constant = 1;
        
        get_token ();
        return 0;
    
    }
    
    if (tok.kind == TOK_CCHAR) {
    
        expr->constant = character_literal_to_long (tok.ident);
        expr->type = 'C';
        expr->is_constant = 1;
        
        get_token ();
        return 0;
    
    }
    
    if (tok.kind == TOK_CINT || tok.kind == TOK_CLONG || tok.kind == TOK_CFLOAT || tok.kind == TOK_CDOUBLE) {
    
        if (tok.kind == TOK_CFLOAT || tok.kind == TOK_CDOUBLE) {
        
            expr->literal_text = xstrdup (tok.ident);
            expr->type = tok.kind == TOK_CFLOAT ? 'F' : 'D';
        
        } else if (tok.kind == TOK_CLONG) {
        
            /**
             * Preserve the complete 64-bit spelling.  expr->constant is a
             * host long and truncates Java long literals on 32-bit builds.
             */
            expr->literal_text = xstrdup (tok.ident);
            expr->constant = field_initializer_to_long (tok.ident);
            expr->type = 'J';
        
        } else {
        
            expr->constant = field_initializer_to_long (tok.ident);
            expr->type = 'I';
        
        }
        
        expr->is_constant = 1;
        get_token ();
        
        return 0;
    
    }
    
    report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
        tok.start, tok.caret, "expected parameter, field, numeric or character literal or parenthesized expression");
    
    return 1;

}

static int parse_body_unary (struct source_names *names, struct method_info *method, struct field_mul_expr *expr) {

    int operation;
    
    if (tok.kind == TOK_INCR || tok.kind == TOK_DECR) {
    
        unsigned long local_index = 0;
        struct local_info *local;
        struct field_info *field;
        
        operation = tok.kind == TOK_INCR ? TOK_PLUS : TOK_MINUS;
        get_token ();
        
        if (tok.kind != TOK_IDENT) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected variable after increment or decrement");
            return 1;
        
        }
        
        local = find_local_info_by_name (method, tok.ident, &local_index);
        field = find_field_info_by_name (names, tok.ident);
        
        if (!local && !field) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown variable '%s'", tok.ident);
            return 1;
        
        }
        
        get_token ();
        
        return make_incdec_expression (method, local, local_index, field, operation, 0, expr);
    
    }
    
    if (tok.kind != TOK_PLUS && tok.kind != TOK_MINUS && tok.kind != TOK_TILDE && tok.kind != TOK_XMARK) {
        return parse_body_primary (names, method, expr);
    }
    
    operation = tok.kind;
    get_token ();
    
    if (parse_body_unary (names, method, expr)) {
        return 1;
    }
    
    if (operation == TOK_PLUS) {
        return 0;
    }
    
    if (operation == TOK_XMARK) {
    
        if (expr->is_constant) {
        
            double value = expr->literal_text ? field_initializer_numeric_value (expr->literal_text) : (double) expr->constant;
            free (expr->literal_text);
            
            expr->literal_text = 0;
            expr->constant = value == 0.0;
            expr->type = 'Z';
            
            return 0;
        
        }
        
        if (!expr->has_field) {
            return 1;
        }
        
        if (expr->factors_len) {
            append_text (&expr->factors, &expr->factors_len, "*");
        }
        
        append_text (&expr->factors, &expr->factors_len, "z");
        expr->type = 'Z';
        
        return 0;
    
    }
    
    if (expr->is_constant) {
    
        if (expr->literal_text) {
        
            char *text;
            unsigned long len;
            
            if (operation == TOK_TILDE) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "operator '~' cannot be applied to a floating-point value");
                return 1;
            
            }
            
            len = strlen (expr->literal_text);
            
            text = xmalloc (len + 2);
            text[0] = '-';
            
            memcpy (text + 1, expr->literal_text, len + 1);
            
            free (expr->literal_text);
            expr->literal_text = text;
        
        } else if (operation == TOK_MINUS) {
            expr->constant = -expr->constant;
        } else {
            expr->constant = ~expr->constant;
        }
        
        return 0;
    
    }
    
    if (!expr->has_field) {
        return 1;
    }
    
    if (expr->factors_len) {
        append_text (&expr->factors, &expr->factors_len, "*");
    }
    
    append_text (&expr->factors, &expr->factors_len, operation == TOK_MINUS ? "N" : "B");
    return 0;

}

static int binary_operator_at_level (int kind, int level) {

    if (level == 0) {
        return kind == TOK_STAR || kind == TOK_BSLASH || kind == TOK_MOD;
    }
    
    if (level == 1) {
        return kind == TOK_PLUS || kind == TOK_MINUS;
    }
    
    if (level == 2) {
        return kind == TOK_LSH || kind == TOK_RSH || kind == TOK_URSH;
    }
    
    if (level == 3) {
        return kind == TOK_LESS || kind == TOK_LTEQ || kind == TOK_GREATER || kind == TOK_GTEQ || kind == TOK_INSTANCEOF;
    }
    
    if (level == 4) {
        return kind == TOK_EQEQ || kind == TOK_NOTEQ;
    }
    
    if (level == 5) {
        return kind == TOK_AMPER;
    }
    
    if (level == 6) {
        return kind == TOK_CARET;
    }
    
    if (level == 7) {
        return kind == TOK_PIPE;
    }
    
    if (level == 8) {
        return kind == TOK_LOGAND;
    }
    
    return kind == TOK_LOGOR;

}

static int merge_string_expression (struct source_names *names, struct method_info *method, struct field_mul_expr *left, struct field_mul_expr *right) {

    const char *left_type = expression_argument_type (names, method, left);
    const char *right_type = expression_argument_type (names, method, right);
    
    const char *return_type;
    unsigned long receiver_local, argument_local, result_local;
    
    char local_name[64], receiver[64], result[64];
    char encoded_args[128];
    
    if (!left_type || !right_type || strcmp (left_type, JAVA_STRING_TYPE) != 0
        || strcmp (right_type, JAVA_STRING_TYPE) != 0) {
        return 1;
    }
    
    return_type = lookup_declared_method_return_type (JAVA_STRING_TYPE, "concat", JAVA_STRING_TYPE, 0);
    
    if (!return_type || strcmp (return_type, JAVA_STRING_TYPE) != 0) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "runtime String concatenation requires a declared concat(String) method");
        return 1;
    
    }
    
    if (add_constructor_argument_local (names, method, left, &receiver_local)
        || add_constructor_argument_local (names, method, right, &argument_local)) {
        return 1;
    }
    
    sprintf (local_name, "__j2d_concat_result_%lu", method->nb_locals);
    
    if (add_local_info (method, local_name, return_type, &result_local)) {
        return 1;
    }
    
    sprintf (receiver, "$%lu", receiver_local);
    sprintf (result, "$%lu", result_local);
    sprintf (encoded_args, "%s|%lu", JAVA_STRING_TYPE, argument_local);
    
    {
    
        char *call_start = xstrdup (tok.start);
        const char *call_caret = call_start + (tok.caret - tok.start);
        
        add_call_statement (method, receiver, "concat", ARG_RESULT_MULTI, encoded_args,
            result_local, get_line_number (), call_start, call_caret, 0, 0);
    
    }
    
    free_field_mul_expr (left);
    left->field = xstrdup (result);
    left->type = 'L';
    left->has_field = 1;
    
    return 0;

}

static int parse_body_binary_level (struct source_names *names, struct method_info *method,

    struct field_mul_expr *expr, int level) {

    if (level == 0) {
    
        if (parse_body_unary (names, method, expr)) {
            return 1;
        }
    
    } else if (parse_body_binary_level (names, method, expr, level - 1)) {
        return 1;
    }
    
    while (binary_operator_at_level (tok.kind, level)) {
    
        struct field_mul_expr right;
        
        int operation = tok.kind;
        int merge_result;
        
        get_token ();
        
        if (operation == TOK_INSTANCEOF) {
        
            char local_name[64], source[64], result[64];
            unsigned long source_local, result_local;
            
            char *type_desc;
            
            if (parse_type_name (names, 0, &type_desc)) {
            
                free_field_mul_expr (expr);
                return 1;
            
            }
            
            if (type_desc[0] != 'L' && type_desc[0] != '[') {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "instanceof requires a reference type");
                
                free_field_mul_expr (expr);
                free (type_desc);
                
                return 1;
            
            }
            
            if (expr->type == 'N' && expr->is_constant) {
            
                free_field_mul_expr (expr);
                
                expr->constant = 0;
                expr->type = 'Z';
                expr->is_constant = 1;
                
                free (type_desc);
                continue;
            
            }
            
            if ((expr->type != 'L' && expr->type != '[') || !expr->has_field
                || add_constructor_argument_local (names, method, expr, &source_local)) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "instanceof requires a reference expression");
                
                free_field_mul_expr (expr);
                free (type_desc);
                
                return 1;
            
            }
            
            sprintf (local_name, "__j2d_instanceof_%lu", method->nb_locals);
            
            if (add_local_info (method, local_name, "Z", &result_local)) {
            
                free_field_mul_expr (expr);
                free (type_desc);
                
                return 1;
            
            }
            
            sprintf (source, "$%lu", source_local);
            add_local_statement (method, result_local, ARG_INSTANCEOF, source, type_desc);
            
            sprintf (result, "$%lu", result_local);
            free_field_mul_expr (expr);
            
            expr->field = xstrdup (result);
            expr->type = 'Z';
            expr->has_field = 1;
            
            free (type_desc);
            continue;
        
        }
        
        if (level == 0) {
        
            if (parse_body_unary (names, method, &right)) {
            
                free_field_mul_expr (expr);
                return 1;
            
            }
        
        } else if (parse_body_binary_level (names, method, &right, level - 1)) {
        
            free_field_mul_expr (expr);
            return 1;
        
        }
        
        if (operation == TOK_PLUS && (expr->type == 'L' || right.type == 'L')) {
            merge_result = merge_string_expression (names, method, expr, &right);
        } else {
            merge_result = merge_field_expression (expr, &right, operation);
        }
        
        if (merge_result) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, merge_result == 2
                    ? "division or remainder by zero in expression"
                    : "operation on these runtime values is not supported yet");
            
            free_field_mul_expr (&right);
            free_field_mul_expr (expr);
            
            return 1;
        
        }
        
        free_field_mul_expr (&right);
    
    }
    
    return 0;

}

static int fold_conditional_expression (struct field_mul_expr *condition, struct field_mul_expr *when_true, struct field_mul_expr *when_false, struct field_mul_expr *expr) {

    int result;
    double condition_value;
    
    if (condition->type != 'Z') {
        return 1;
    }
    
    if (condition->is_constant) {
    
        condition_value = condition->literal_text ? field_initializer_numeric_value (condition->literal_text) : (double) condition->constant;
        
        if (condition_value != 0.0) {
        
            *expr = *when_true;
            memset (when_true, 0, sizeof (*when_true));
        
        } else {
        
            *expr = *when_false;
            memset (when_false, 0, sizeof (*when_false));
        
        }
        
        return 0;
    
    }
    
    result = merge_field_expression (when_true, when_false, TOK_MINUS);
    
    if (result) {
        return result;
    }
    
    result = merge_field_expression (condition, when_true, TOK_STAR);
    
    if (result) {
        return result;
    }
    
    result = merge_field_expression (when_false, condition, TOK_PLUS);
    
    if (result) {
        return result;
    }
    
    *expr = *when_false;
    memset (when_false, 0, sizeof (*when_false));
    
    return 0;

}

static int append_conditional_result (struct source_names *names, struct method_info *method, unsigned long result_local, struct field_mul_expr *value) {

    if (value->has_field) {
    
        add_local_statement (method, result_local, ARG_ARITHMETIC, value->field,
            value->factors ? value->factors : "1");
        
        return 0;
    
    }
    
    if (value->is_constant) {
    
        const char *literal = value->literal_text;
        char number[64];
        
        if (!literal) {
        
            sprintf (number, "%ld", value->constant);
            literal = number;
        
        }
        
        add_local_statement (method, result_local, ARG_INT, 0, literal);
        return 0;
    
    }
    
    (void) names;
    return 1;

}

static int parse_body_conditional_tail (struct source_names *names, struct method_info *method, struct field_mul_expr *expr, int question_consumed) {

    struct field_mul_expr when_true, when_false;
    
    const char *true_type, *false_type;
    unsigned long false_jump, end_jump, false_label, end_label, result_local;
    
    char local_name[64], result_source[64];
    
    if (!question_consumed) {
    
        if (tok.kind != TOK_QMARK) {
            return 0;
        }
        
        get_token ();
    
    }
    
    if (expr->type != 'Z') {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "conditional expression requires a boolean condition");
        
        free_field_mul_expr (expr);
        return 1;
    
    }
    
    false_jump = method->nb_statements;
    
    if (expr->has_field) {
    
        struct statement_info *stmt = append_control_statement (method, STMT_IF_FALSE);
        
        stmt->target = xstrdup (expr->field);
        stmt->arg_text = xstrdup (expr->factors ? expr->factors : "1");
    
    } else {
    
        if (!expr->is_constant) {
        
            free_field_mul_expr (expr);
            return 1;
        
        }
        
        if (expr->constant) {
            false_jump = (unsigned long) -1;
        } else {
            append_control_statement (method, STMT_GOTO);
        }
    
    }
    
    if (parse_body_expression (names, method, &when_true)) {
    
        free_field_mul_expr (expr);
        return 1;
    
    }
    
    true_type = expression_argument_type (names, method, &when_true);
    
    if (!true_type) {
    
        free_field_mul_expr (&when_true);
        free_field_mul_expr (expr);
        
        return 1;
    
    }
    
    sprintf (local_name, "__j2d_conditional_%lu", method->nb_locals);
    
    if (add_local_info (method, local_name, true_type, &result_local)
        || append_conditional_result (names, method, result_local, &when_true)
            || expect_token (TOK_COLON, "':' in conditional expression")) {
    
        free_field_mul_expr (&when_true);
        free_field_mul_expr (expr);
        
        return 1;
    
    }
    
    end_jump = method->nb_statements;
    append_control_statement (method, STMT_GOTO);
    
    false_label = method->nb_statements;
    append_control_statement (method, STMT_LABEL);
    
    if (false_jump != (unsigned long) -1) {
        method->statements[false_jump].jump_target = false_label;
    }
    
    get_token ();
    
    if (parse_body_expression (names, method, &when_false)) {
    
        free_field_mul_expr (&when_true);
        free_field_mul_expr (expr);
        
        return 1;
    
    }
    
    false_type = expression_argument_type (names, method, &when_false);
    
    if (!false_type || strcmp (true_type, false_type) != 0
        || append_conditional_result (names, method, result_local, &when_false)) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "conditional expression has incompatible result types");
        
        free_field_mul_expr (&when_false);
        free_field_mul_expr (&when_true);
        free_field_mul_expr (expr);
        
        return 1;
    
    }
    
    end_label = method->nb_statements;
    append_control_statement (method, STMT_LABEL);
    
    method->statements[end_jump].jump_target = end_label;
    
    sprintf (result_source, "$%lu", result_local);
    free_field_mul_expr (expr);
    
    expr->field = xstrdup (result_source);
    expr->type = method->locals[result_local].type[0];
    expr->has_field = 1;
    
    free_field_mul_expr (&when_true);
    free_field_mul_expr (&when_false);
    
    return 0;

}

static int parse_body_expression (struct source_names *names, struct method_info *method, struct field_mul_expr *expr) {

    
    if (tok.kind == TOK_IDENT && peek_token_kind () == TOK_ASSIGN) {
    
        struct local_info *local;
        struct field_info *field;
        struct field_mul_expr value;
        
        unsigned long local_index = 0, param_index = 0, value_index, converted_index;
        int is_param;
        
        char *name = xstrdup (tok.ident);
        char *target_type;
        
        const char *actual_type;
        char source[64], result[64], converted_name[64];
        
        const char *start = xstrdup (tok.start);
        const char *caret = start + (tok.caret - tok.start);
        
        unsigned long line = get_line_number ();
        int widening = 0;
        
        memset (expr, 0, sizeof (*expr));
        
        local = find_local_info_by_name (method, name, &local_index);
        is_param = !local && !find_param_index (method, name, &param_index);
        field = !local && !is_param ? find_field_info_by_name (names, name) : 0;
        
        if (!local && !is_param && !field) {
        
            report_line_at (get_filename (), line, REPORT_ERROR, start, caret, "unknown variable '%s'", name);
            
            free ((char *) start);
            free (name);
            
            return 1;
        
        }
        
        if (field && (method->flags & METHOD_STATIC) && !(field->flags & METHOD_STATIC)) {
        
            report_line_at (get_filename (), line, REPORT_ERROR, start, caret,
                "non-static field '%s' cannot be referenced from a static method", name);
            
            free ((char *) start);
            free (name);
            
            return 1;
        
        }
        
        target_type = xstrdup (local ? local->type : is_param ? method_param_type (method, param_index) : field->type);
        
        get_token ();
        get_token ();
        
        if (parse_body_expression (names, method, &value)) {
        
            free ((char *) start);
            free (target_type);
            free (name);
            
            return 1;
        
        }
        
        actual_type = expression_argument_type (names, method, &value);
        
        if (!actual_type && value.has_field && value.field && value.field[0] == '#') {
            actual_type = method_param_type (method, strtoul (value.field + 1, 0, 10));
        }
        
        if (value.type == 'N' && (target_type[0] == 'L' || target_type[0] == '[')) {
        
            sprintf (converted_name, "__j2d_assignment_%lu", method->nb_locals);
            
            if (add_local_info (method, converted_name, target_type, &value_index)) {
            
                free_field_mul_expr (&value);
                free ((char *) start);
                free (target_type);
                free (name);
                
                return 1;
            
            }
            
            add_local_statement (method, value_index, ARG_INT, 0, "0");
        
        } else {
        
            if (actual_type && (target_type[0] == 'L' || target_type[0] == '[')
                && (actual_type[0] == 'L' || actual_type[0] == '[')) {
            
                widening = declared_reference_assignable (actual_type, target_type);
            
            } else if (actual_type && target_type[0] != 'L' && target_type[0] != '[') {
                widening = primitive_widening_assignable (actual_type[0], target_type[0]);
            }
            
            if (!widening) {
            
                report_line_at (get_filename (), line, REPORT_ERROR, start, caret,
                    "incompatible types in assignment to '%s'", name);
                
                free_field_mul_expr (&value);
                free ((char *) start);
                free (target_type);
                free (name);
                
                return 1;
            
            }
            
            if (add_constructor_argument_local (names, method, &value, &value_index)) {
            
                free_field_mul_expr (&value);
                free ((char *) start);
                free (target_type);
                free (name);
                
                return 1;
            
            }
        
        }
        
        if (strcmp (method->locals[value_index].type, target_type) != 0) {
        
            char operation[4];
            sprintf (converted_name, "__j2d_assignment_%lu", method->nb_locals);
            
            if (add_local_info (method, converted_name, target_type, &converted_index)) {
            
                free_field_mul_expr (&value);
                free ((char *) start);
                free (target_type);
                free (name);
                
                return 1;
            
            }
            
            sprintf (source, "$%lu", value_index);
            
            operation[0] = '!';
            operation[1] = target_type[0];
            operation[2] = '\0';
            
            add_local_statement (method, converted_index, ARG_ARITHMETIC, source,
                (target_type[0] == 'L' || target_type[0] == '[') ? "" : operation);
            value_index = converted_index;
        
        }
        
        sprintf (source, "$%lu", value_index);
        
        if (local) {
            add_local_statement (method, local_index, ARG_ARITHMETIC, source, "");
        } else if (is_param) {
            add_parameter_statement (method, param_index, ARG_ARITHMETIC, source, "");
        } else {
            add_field_statement (method, field->name, ARG_ARITHMETIC, source, "");
        }
        
        free_field_mul_expr (&value);
        sprintf (result, "$%lu", value_index);
        
        expr->field = xstrdup (result);
        expr->type = target_type[0];
        expr->has_field = 1;
        
        free ((char *) start);
        free (target_type);
        free (name);
        
        return 0;
    
    }
    
    if (parse_body_binary_level (names, method, expr, 9)) {
        return 1;
    }
    
    return parse_body_conditional_tail (names, method, expr, 0);

}

static int parse_return_statement (struct source_names *names, struct method_info *method) {

    struct statement_info *stmt;
    
    unsigned long param_index;
    unsigned long stmt_index;
    
    char *target = 0;
    char *name = 0;
    char *text = 0;
    
    get_token ();
    
    method->statements = xrealloc (method->statements, (method->nb_statements + 1) * sizeof (*method->statements));
    
    stmt_index = method->nb_statements++;
    stmt = &method->statements[stmt_index];
    
    memset (stmt, 0, sizeof (*stmt));
    stmt->kind = STMT_RETURN;
    
    if (strcmp (method->return_type, "V") == 0) {
    
        if (tok.kind != TOK_SEMI) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "void method cannot return a value");
            return 1;
        
        }
        
        return 0;
    
    }
    
    if (tok.kind == TOK_SEMI) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "non-void method must return a value");
        return 1;
    
    }
    
    if (tok.kind == TOK_THIS && peek_token_kind () == TOK_SEMI) {
    
        char *class_type = dex_class_descriptor (names);
        int compatible = strcmp (class_type, method->return_type) == 0;
        
        free (class_type);
        
        if ((method->flags & METHOD_STATIC) || !compatible) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "cannot return 'this' from this method");
            return 1;
        
        }
        
        get_token ();
        
        if (expect_token (TOK_SEMI, "';' after return expression")) {
            return 1;
        }
        
        stmt->arg_kind = ARG_THIS;
        return 0;
    
    }
    
    if (tok.kind == TOK_PPSTR) {
    
        text = copy_string_literal (tok.ident);
        get_token ();
        
        if (expect_token (TOK_SEMI, "';'") ) {
        
            free (text);
            return 1;
        
        }
        
        stmt->arg_kind = ARG_STRING;
        stmt->arg_text = text;
        
        return 0;
    
    }
    
    if (tok.kind == TOK_LPAREN || tok.kind == TOK_NEW || tok.kind == TOK_CCHAR || tok.kind == TOK_CINT || tok.kind == TOK_CLONG
        || tok.kind == TOK_CFLOAT || tok.kind == TOK_CDOUBLE || tok.kind == TOK_TRUE || tok.kind == TOK_FALSE
            || tok.kind == TOK_IDENT || tok.kind == TOK_THIS || tok.kind == TOK_INCR || tok.kind == TOK_DECR
                || tok.kind == TOK_PLUS || tok.kind == TOK_MINUS) {
    
        struct field_mul_expr expr;
        
        if (parse_body_expression (names, method, &expr)) {
            return 1;
        }
        
        if (method->nb_statements > stmt_index + 1) {
        
            stmt = &method->statements[stmt_index];
            memset (stmt, 0, sizeof (*stmt));
            
            stmt->kind = STMT_LABEL;
            
            method->statements = xrealloc (method->statements, (method->nb_statements + 1) * sizeof (*method->statements));
            stmt = &method->statements[method->nb_statements++];
            
            memset (stmt, 0, sizeof (*stmt));
            stmt->kind = STMT_RETURN;
        
        } else {
            stmt = &method->statements[stmt_index];
        }
        
        if (tok.kind != TOK_SEMI) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "expected ';' after return expression");
            
            free_field_mul_expr (&expr);
            return 1;
        
        }
        
        if ((method->return_type[0] == 'Z') != (expr.type == 'Z')) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "incompatible boolean return expression");
            
            free_field_mul_expr (&expr);
            return 1;
        
        }
        
        if ((method->return_type[0] == 'I' || method->return_type[0] == 'S'
            || method->return_type[0] == 'J' || method->return_type[0] == 'B'
                || method->return_type[0] == 'C')
                    && (expr.type == 'D' || expr.type == 'F')) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "incompatible types: possible lossy conversion in return expression");
            
            free_field_mul_expr (&expr);
            return 1;
        
        }
        
        if (!expr.has_field) {
        
            const char *literal = expr.literal_text;
            char tmp[64];
            
            if (!literal) {
            
                sprintf (tmp, "%ld", expr.constant);
                literal = tmp;
            
            }
            
            stmt->arg_kind = ARG_INT;
            stmt->arg_text = xstrdup (literal);
            
            free_field_mul_expr (&expr);
            return 0;
        
        }
        
        if (!expr.factors) {
            expr.factors = xstrdup ("1");
        }
        
        stmt->arg_kind = ARG_ARITHMETIC;
        stmt->target = expr.field;
        stmt->arg_text = expr.factors;
        
        expr.field = 0;
        expr.factors = 0;
        
        free_field_mul_expr (&expr);
        return 0;
    
    }
    
    if (tok.kind == TOK_CINT || tok.kind == TOK_CLONG || tok.kind == TOK_MINUS) {
    
        int neg = 0;
        
        if (tok.kind == TOK_MINUS) {
        
            neg = 1;
            get_token ();
            
            if (tok.kind != TOK_CINT && tok.kind != TOK_CLONG) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected integer literal after '-'");
                return 1;
            
            }
        
        }
        
        if (neg) {
        
            unsigned long len = strlen (tok.ident);
            
            text = xmalloc (len + 2);
            text[0] = '-';
            
            memcpy (text + 1, tok.ident, len + 1);
        
        } else {
            text = xstrdup (tok.ident);
        }
        
        get_token ();
        
        if (expect_token (TOK_SEMI, "';'") ) {
        
            free (text);
            return 1;
        
        }
        
        stmt->arg_kind = ARG_INT;
        stmt->arg_text = text;
        
        return 0;
    
    }
    
    if (tok.kind != TOK_IDENT) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected return expression");
        return 1;
    
    }
    
    target = xstrdup (tok.ident);
    get_token ();
    
    if (tok.kind == TOK_SEMI) {
    
        if (find_param_index (method, target, &param_index)) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown return value '%s'", target);
            
            free (target);
            return 1;
        
        }
        
        stmt->arg_kind = ARG_PARAM;
        stmt->arg_param_index = param_index;
        
        free (target);
        return 0;
    
    }
    
    if (tok.kind == TOK_LPAREN) {
    
        get_token ();
        
        if (expect_token (TOK_RPAREN, "')'") ) {
        
            free (target);
            return 1;
        
        }
        
        get_token ();
        
        if (expect_token (TOK_SEMI, "';'") ) {
        
            free (target);
            return 1;
        
        }
        
        stmt->arg_kind = ARG_CALL;
        stmt->target = 0;
        stmt->method = target;
        
        return 0;
    
    }
    
    if (tok.kind != TOK_DOT) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected ';', '(' or '.' in return expression");
        
        free (target);
        return 1;
    
    }
    
    get_token ();
    
    if (expect_ident ("method name")) {
    
        free (target);
        return 1;
    
    }
    
    name = xstrdup (tok.ident);
    get_token ();
    
    if (expect_token (TOK_LPAREN, "'('") ) {
    
        free (target);
        free (name);
        
        return 1;
    
    }
    
    get_token ();
    
    if (expect_token (TOK_RPAREN, "')'") ) {
    
        free (target);
        free (name);
        
        return 1;
    
    }
    
    get_token ();
    
    if (expect_token (TOK_SEMI, "';'") ) {
    
        free (target);
        free (name);
        
        return 1;
    
    }
    
    stmt->arg_kind = ARG_CALL;
    stmt->target = target;
    stmt->method = name;
    
    return 0;

}

static const char *expression_type_descriptor (char type) {

    switch (type) {
    
        case 'Z': case 'B': case 'C': case 'S': case 'I':
        case 'J': case 'F': case 'D':
        
            break;
        
        default:
        
            return 0;
    
    }
    
    {
    
        static char desc[2];
        
        desc[0] = type;
        desc[1] = '\0';
        
        return desc;
    
    }

}

static const char *expression_argument_type (struct source_names *names, struct method_info *method, const struct field_mul_expr *expr) {

    const char *type = expression_type_descriptor (expr->type);
    
    if (!type && (expr->type == '[' || expr->type == 'L') && expr->has_field
        && expr->field && expr->field[0] == '$'
            && (!expr->factors || strcmp (expr->factors, "1") == 0)) {
    
        unsigned long index = strtoul (expr->field + 1, 0, 10);
        
        if (index < method->nb_locals && method->locals[index].type[0] == expr->type) {
            type = method->locals[index].type;
        }
    
    }
    
    if (!type && (expr->type == '[' || expr->type == 'L') && expr->has_field
        && expr->field && expr->field[0] != '$' && expr->field[0] != '#'
            && (!expr->factors || strcmp (expr->factors, "1") == 0)) {
    
        struct field_info *field = find_field_info_by_name (names, expr->field);
        
        if (field && field->type[0] == expr->type) {
            type = field->type;
        }
    
    }
    
    return type;

}

static const char *descriptor_type_name (char type) {

    switch (type) {
    
        case 'Z':
        
            return "boolean";
        
        case 'B':
        
            return "byte";
        
        case 'C':
        
            return "char";
        
        case 'S':
        
            return "short";
        
        case 'I':
        
            return "int";
        
        case 'J':
        
            return "long";
        
        case 'F':
        
            return "float";
        
        case 'D':
        
            return "double";
        
        case 'V':
        
            return "void";
    
    }

    return 0;

}

static char *format_argument_types (const char *params) {

    const char *p = params;
    char *result, *out;
    
    unsigned long capacity = strlen (params) * 3 + 32;
    int first = 1;
    
    result = xmalloc (capacity);
    out = result;
    
    *out++ = '(';
    
    while (*p) {
    
        const char *name;
        unsigned long arrays = 0;
        
        if (!first) {
        
            memcpy (out, ", ", 2);
            out += 2;
        
        }
        
        first = 0;
        
        while (*p == '[') {
        
            arrays++;
            p++;
        
        }
        
        name = descriptor_type_name (*p);
        
        if (name) {
        
            unsigned long len = strlen (name);
            memcpy (out, name, len);
            
            out += len;
            p++;
        
        } else if (*p == 'L') {
        
            const char *start = ++p;
            const char *simple = start;
            
            while (*p && *p != ';') {
            
                if (*p == '/') {
                    simple = p + 1;
                }
                
                p++;
            
            }
            
            memcpy (out, simple, p - simple);
            out += p - simple;
            
            if (*p == ';') {
                p++;
            }
        
        } else {
            *out++ = *p++;
        }
        
        while (arrays--) {
        
            *out++ = '[';
            *out++ = ']';
        
        }
    
    }
    
    *out++ = ')';
    *out = '\0';
    
    return result;

}

static int add_constructor_argument_local (struct source_names *names, struct method_info *method, struct field_mul_expr *expr, unsigned long *local_index_out) {

    const char *type = expression_argument_type (names, method, expr);
    char name[64];
    
    if (!type) {
        return 1;
    }
    
    sprintf (name, "__j2d_ctor_arg_%lu", method->nb_locals);
    
    if (add_local_info (method, name, type, local_index_out)) {
        return 1;
    }
    
    if (!expr->has_field) {
    
        const char *literal = expr->literal_text;
        char value[64];
        
        if (!literal) {
        
            sprintf (value, "%ld", expr->constant);
            literal = value;
        
        }
        
        add_local_statement (method, *local_index_out, ARG_INT, 0, literal);
    
    } else {
    
        if (!expr->factors) {
            expr->factors = xstrdup ("1");
        }
        
        add_local_statement (method, *local_index_out, ARG_ARITHMETIC, expr->field, expr->factors);
    
    }
    
    return 0;

}

static int split_constructor_arguments (const char *text, char **params_out, unsigned long **locals_out, unsigned long *nb_locals_out) {

    const char *sep;
    const char *p;
    unsigned long len;
    
    *params_out = 0;
    *locals_out = 0;
    *nb_locals_out = 0;
    
    if (!text) {
    
        *params_out = xstrdup ("");
        return 0;
    
    }
    
    sep = strchr (text, '|');
    
    if (!sep) {
        return 1;
    }
    
    len = (unsigned long) (sep - text);
    *params_out = xmalloc (len + 1);
    
    memcpy (*params_out, text, len);
    (*params_out)[len] = '\0';
    
    p = sep + 1;
    
    while (*p) {
    
        char *end;
        unsigned long value = strtoul (p, &end, 10);
        
        if (end == p || (*end && *end != ',')) {
        
            free (*params_out);
            free (*locals_out);
            
            *params_out = 0;
            *locals_out = 0;
            *nb_locals_out = 0;
            
            return 1;
        
        }
        
        *locals_out = xrealloc (*locals_out, (*nb_locals_out + 1) * sizeof (**locals_out));
        (*locals_out)[(*nb_locals_out)++] = value;
        
        p = *end == ',' ? end + 1 : end;
    
    }
    
    return 0;

}

static int argument_begins_with_string (struct source_names *names, struct method_info *method) {

    (void) names;
    (void) method;
    
    return tok.kind == TOK_PPSTR;

}

static unsigned long count_descriptor_params (const char *p);

static int declared_reference_assignable (const char *actual, const char *formal) {

    unsigned long depth;
    const char *current = actual;
    
    if (!actual || !formal) {
        return 0;
    }
    
    if (strcmp (actual, formal) == 0) {
        return 1;
    }
    
    if (strcmp (formal, JAVA_OBJECT_TYPE) == 0 && (actual[0] == 'L' || actual[0] == '[')) {
        return 1;
    }
    
    if (actual[0] == '[' || formal[0] == '[') {
        return 0;
    }
    
    for (depth = 0; depth < nb_declaration_names; depth++) {
    
        unsigned long i;
        const char *super_type = 0;
        
        for (i = 0; i < nb_declaration_names; i++) {
        
            char *desc = dex_class_descriptor (&declaration_names[i]);
            int match = strcmp (desc, current) == 0;
            
            free (desc);
            
            if (match) {
            
                super_type = declaration_names[i].super_class;
                break;
            
            }
        
        }
        
        if (!super_type || strcmp (super_type, current) == 0) {
            break;
        }
        
        if (strcmp (super_type, formal) == 0) {
            return 1;
        }
        
        current = super_type;
    
    }
    
    return 0;

}

static int primitive_widening_assignable (char actual, char formal) {

    if (actual == formal) {
        return 1;
    }
    
    switch (actual) {
    
        case 'B':
        
            return formal == 'S' || formal == 'I' || formal == 'J' || formal == 'F' || formal == 'D';
        
        case 'S':   case 'C':
        
            return formal == 'I' || formal == 'J' || formal == 'F' || formal == 'D';
        
        case 'I':
        
            return formal == 'J' || formal == 'F' || formal == 'D';
        
        case 'J':
        
            return formal == 'F' || formal == 'D';
        
        case 'F':
        
            return formal == 'D';
    
    }
    
    return 0;

}

static int declared_parameters_assignable (const char *actual, const char *formal) {

    while (*actual && *formal) {
    
        char *actual_type = copy_first_type_descriptor (actual);
        char *formal_type = copy_first_type_descriptor (formal);
        
        int compatible = 0;
        
        if (!actual_type || !formal_type) {
        
            free (actual_type);
            free (formal_type);
            
            return 0;
        
        }
        
        if (actual_type[0] == 'N' && actual_type[1] == '\0' && (formal_type[0] == 'L' || formal_type[0] == '[')) {
            compatible = 1;
        } else if ((actual_type[0] == 'L' || actual_type[0] == '[') && (formal_type[0] == 'L' || formal_type[0] == '[')) {
            compatible = declared_reference_assignable (actual_type, formal_type);
        } else if (strlen (actual_type) == 1 && strlen (formal_type) == 1) {
            compatible = primitive_widening_assignable (actual_type[0], formal_type[0]);
        } else {
            compatible = strcmp (actual_type, formal_type) == 0;
        }
        
        actual += strlen (actual_type);
        formal += strlen (formal_type);
        
        free (actual_type);
        free (formal_type);
        
        if (!compatible) {
            return 0;
        }
    
    }
    
    return *actual == '\0' && *formal == '\0';

}

static const char *declared_compatible_method_params (const char *class_desc, const char *method_name, const char *actual_params) {

    const char *current = class_desc;
    const char *best = 0;
    
    unsigned long depth;
    int ambiguous = 0;
    
    for (depth = 0; current && depth < nb_declaration_names; depth++) {
    
        unsigned long i, j;
        const char *super_type = 0;
        
        for (i = 0; i < nb_declaration_names; i++) {
        
            char *desc = dex_class_descriptor (&declaration_names[i]);
            int match = strcmp (desc, current) == 0;
            
            free (desc);
            
            if (!match) {
                continue;
            }
            
            super_type = declaration_names[i].super_class;
            
            for (j = 0; j < declaration_names[i].nb_methods; j++) {
            
                const struct method_info *candidate = &declaration_names[i].methods[j];
                const char *params = candidate->params;
                
                if (strcmp (candidate->java_name, method_name) != 0 || !declared_parameters_assignable (actual_params, params)) {
                    continue;
                }
                
                if (strcmp (actual_params, params) == 0) {
                   return params;
                }
                
                if (!best) {
                    best = params;
                } else if (strcmp (best, params) != 0) {
                
                    if (declared_parameters_assignable (params, best)) {
                    
                        best = params;
                        ambiguous = 0;
                    
                    } else if (!declared_parameters_assignable (best, params)) {
                        ambiguous = 1;
                    }
                
                }
            
            }
            
            break;
        
        }
        
        if (i == nb_declaration_names || !super_type || strcmp (super_type, current) == 0) {
            break;
        }
        
        current = super_type;
    
    }
    
    return ambiguous ? 0 : best;

}

static const char *declared_reference_call_parameter (const char *class_desc, const char *method_name) {

    const char *literal_type = JAVA_STRING_TYPE;
    int ambiguous = 0;
    
    const char *result = 0;
    unsigned long i, j;
    
    if (!class_desc || !method_name) {
        return 0;
    }
    
    for (i = 0; i < nb_declaration_names; i++) {
    
        char *candidate = dex_class_descriptor (&declaration_names[i]);
        int match = strcmp (candidate, class_desc) == 0;
        
        free (candidate);
        
        if (!match) {
            continue;
        }
        
        for (j = 0; j < declaration_names[i].nb_methods; j++) {
        
            const struct method_info *method = &declaration_names[i].methods[j];
            const char *params = method->params;
            
            if (strcmp (method->java_name, method_name) != 0 || (params[0] != 'L' && params[0] != '[')) {
                continue;
            }
            
            if (count_descriptor_params (params) != 1) {
                continue;
            }
            
            if (!declared_reference_assignable (literal_type, params)) {
                continue;
            }
            
            if (!result) {
                result = params;
            } else if (strcmp (result, params) != 0) {
            
                if (declared_reference_assignable (params, result)) {
                
                    result = params;
                    ambiguous = 0;
                
                } else if (!declared_reference_assignable (result, params)) {
                    ambiguous = 1;
                }
            
            }
        
        }
    
    }
    
    if (result) {
    
        for (i = 0; i < nb_declaration_names; i++) {
        
            char *candidate = dex_class_descriptor (&declaration_names[i]);
            int match = strcmp (candidate, class_desc) == 0;
            
            free (candidate);
            
            if (!match) {
                continue;
            }
            
            for (j = 0; j < declaration_names[i].nb_methods; j++) {
            
                const struct method_info *method = &declaration_names[i].methods[j];
                const char *params = method->params;
                
                if (strcmp (method->java_name, method_name) == 0
                    && (params[0] == 'L' || params[0] == '[') && count_descriptor_params (params) == 1
                        && declared_reference_assignable (literal_type, params) && !declared_reference_assignable (result, params)) {
                    ambiguous = 1;
                }
            
            }
        
        }
    
    }
    
    return ambiguous ? 0 : result;

}

static int parse_conditional_string_argument (struct source_names *names, struct method_info *method, struct field_mul_expr *condition, unsigned long *local_index_out, const char *reference_type) {

    unsigned long false_jump = (unsigned long) -1;
    unsigned long end_jump, false_label, end_label;
    
    char local_name[64];
    char *value = 0;
    
    int dynamic = 0;
    
    struct concat_field_ref *refs = 0;
    unsigned long nb_refs = 0;
    
    if (condition->type != 'Z') {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "conditional expression requires a boolean condition");
        return 1;
    
    }
    
    sprintf (local_name, "__j2d_call_arg_%lu", method->nb_locals);
    
    if (add_local_info (method, local_name, reference_type, local_index_out)) {
        return 1;
    }
    
    if (condition->has_field) {
    
        struct statement_info *stmt = append_control_statement (method, STMT_IF_FALSE);
        
        stmt->target = xstrdup (condition->field);
        stmt->arg_text = xstrdup (condition->factors ? condition->factors : "1");
        
        false_jump = method->nb_statements - 1;
    
    } else if (!condition->is_constant) {
        return 1;
    } else if (condition->constant == 0) {
    
        false_jump = method->nb_statements;
        append_control_statement (method, STMT_GOTO);
    
    }
    
    if (parse_string_concat_argument (names, method, &value, &dynamic, &refs, &nb_refs)) {
        return 1;
    }
    
    add_local_statement (method, *local_index_out, dynamic ? ARG_STRING_CONCAT : ARG_STRING, 0, value);
    
    free_concat_field_refs (refs, nb_refs);
    free (value);
    
    if (expect_token (TOK_COLON, "':' in conditional expression")) {
        return 1;
    }
    
    end_jump = method->nb_statements;
    append_control_statement (method, STMT_GOTO);
    
    false_label = method->nb_statements;
    append_control_statement (method, STMT_LABEL);
    
    if (false_jump != (unsigned long) -1) {
        method->statements[false_jump].jump_target = false_label;
    }
    
    get_token ();
    
    value = 0;
    refs = 0;
    
    nb_refs = 0;
    dynamic = 0;
    
    if (parse_string_concat_argument (names, method, &value, &dynamic, &refs, &nb_refs)) {
        return 1;
    }
    
    add_local_statement (method, *local_index_out, dynamic ? ARG_STRING_CONCAT : ARG_STRING, 0, value);
    
    free_concat_field_refs (refs, nb_refs);
    free (value);
    
    end_label = method->nb_statements;
    append_control_statement (method, STMT_LABEL);
    
    method->statements[end_jump].jump_target = end_label;
    return 0;

}

static int parse_call_arguments (struct source_names *names, struct method_info *method, char **encoded_out, const char *target_type, const char *called_name) {

    unsigned long args_len = 0, params_len = 0;
    
    char *args = 0, *params = 0;
    int first = 1;
    
    int has_null_argument = 0;
    
    while (tok.kind != TOK_RPAREN) {
    
        unsigned long local_index;
        char tmp[64];
        
        if (!first) {
        
            if (expect_token (TOK_COMMA, "',' between method arguments")) {
                goto fail;
            }
            
            get_token ();
        
        }
        
        {
        
            const char *literal_type = 0;
            int string_argument = argument_begins_with_string (names, method);
            
            if (string_argument) {
            
                literal_type = declared_reference_call_parameter (target_type, called_name);
                
                if (!literal_type) {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret,
                        "cannot resolve a unique reference parameter for '%s' from source declarations", called_name ? called_name : "<call>");
                    
                    goto fail;
                
                }
            
            }
            
            if (tok.kind == TOK_NULL) {
            
                char local_name[64];
                
                sprintf (local_name, "__j2d_call_arg_%lu", method->nb_locals);
                
                if (add_local_info (method, local_name, JAVA_OBJECT_TYPE, &local_index)) {
                    goto fail;
                }
                
                add_local_statement (method, local_index, ARG_INT, 0, "0");
                
                append_text (&params, &params_len, "N");
                has_null_argument = 1;
                
                get_token ();
            
            } else if (tok.kind == TOK_NEW) {
            
                char *new_name;
                char *new_type;
                char *ctor_args = 0;
                char local_name[64];
                
                get_token ();
                
                if (expect_ident ("class name after 'new'")) {
                    goto fail;
                }
                
                new_name = xstrdup (tok.ident);
                new_type = resolve_class_type (names, new_name);
                
                get_token ();
                
                if (expect_token (TOK_LPAREN, "'(' after class name")) {
                
                    free (new_name);
                    free (new_type);
                    
                    goto fail;
                
                }
                
                get_token ();
                
                if (parse_call_arguments (names, method, &ctor_args, new_type, "<init>")) {
                
                    free (new_name);
                    free (new_type);
                    
                    goto fail;
                
                }
                
                get_token ();
                sprintf (local_name, "__j2d_call_new_%lu", method->nb_locals);
                
                if (add_local_info (method, local_name, new_type, &local_index)) {
                
                    free (ctor_args);
                    free (new_name);
                    free (new_type);
                    
                    goto fail;
                
                }
                
                add_local_statement (method, local_index, ARG_NEW, new_name, ctor_args);
                append_text (&params, &params_len, new_type);
                
                free (ctor_args);
                free (new_name);
                free (new_type);
            
            } else if (string_argument) {
            
                char *value = 0;
                int dynamic = 0;
                
                struct concat_field_ref *refs = 0;
                unsigned long nb_refs = 0;
                
                char local_name[64];
                
                if (parse_string_concat_argument (names, method, &value, &dynamic, &refs, &nb_refs)) {
                    goto fail;
                }
                
                sprintf (local_name, "__j2d_call_arg_%lu", method->nb_locals);
                
                if (add_local_info (method, local_name, literal_type, &local_index)) {
                
                    free_concat_field_refs (refs, nb_refs);
                    free (value);
                    
                    goto fail;
                
                }
                
                add_local_statement (method, local_index, dynamic ? ARG_STRING_CONCAT : ARG_STRING, 0, value);
                
                free_concat_field_refs (refs, nb_refs);
                free (value);
                
                append_text (&params, &params_len, literal_type);
            
            } else {
            
                struct field_mul_expr expr;
                const char *arg_type;
                
                if (parse_body_binary_level (names, method, &expr, 9)) {
                    goto fail;
                }
                
                if (tok.kind == TOK_QMARK) {
                
                    get_token ();
                    
                    if (argument_begins_with_string (names, method)) {
                    
                        if (!declared_reference_call_parameter (target_type, called_name)) {
                        
                            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret,
                                "cannot resolve reference parameter type from source declarations");
                            
                            goto fail;
                        
                        }
                        
                        if (parse_conditional_string_argument (names, method, &expr, &local_index, declared_reference_call_parameter (target_type, called_name))) {
                        
                            free_field_mul_expr (&expr);
                            goto fail;
                        
                        }
                        
                        append_text (&params, &params_len, declared_reference_call_parameter (target_type, called_name));
                        
                        free_field_mul_expr (&expr);
                        goto have_argument;
                    
                    }
                    
                    if (parse_body_conditional_tail (names, method, &expr, 1)) {
                        goto fail;
                    }
                
                }
                
                arg_type = expression_argument_type (names, method, &expr);
                
                if (!arg_type || add_constructor_argument_local (names, method, &expr, &local_index)) {
                
                    free_field_mul_expr (&expr);
                    
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unsupported method argument type");
                    goto fail;
                
                }
                
                append_text (&params, &params_len, arg_type);
                free_field_mul_expr (&expr);
            
            }
        
        }
        
    have_argument:
        
        if (!first) {
            append_text (&args, &args_len, ",");
        }
        
        sprintf (tmp, "%lu", local_index);
        
        append_text (&args, &args_len, tmp);
        first = 0;
        
        if (tok.kind != TOK_COMMA && tok.kind != TOK_RPAREN) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected ',' or ')' after method argument");
            goto fail;
        
        }
    
    }
    
    if (!params) {
        params = xstrdup ("");
    }
    
    if (!args) {
        args = xstrdup ("");
    }
    
    {
    
        const char *resolved = declared_compatible_method_params (target_type, called_name, params);
        
        if (!resolved && has_null_argument) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret,
                "no unique reference overload of '%s' accepts null", called_name ? called_name : "<call>");
            goto fail;
        
        }
        
        if (resolved && strcmp (resolved, params) != 0) {
        
            const char *actual = params;
            const char *formal = resolved;
            const char *arg = args;
            
            char *converted_args = 0;
            unsigned long converted_len = 0;
            
            int first_arg = 1;
            
            while (*actual && *formal && arg && *arg) {
            
                char *actual_type = copy_first_type_descriptor (actual);
                char *formal_type = copy_first_type_descriptor (formal);
                char *end;
                
                unsigned long index = strtoul (arg, &end, 10);
                unsigned long converted_index = index;
                
                char tmp[64];
                
                if (!actual_type || !formal_type || end == arg || (*end && *end != ',')) {
                
                    free (converted_args);
                    
                    free (actual_type);
                    free (formal_type);
                    
                    goto fail;
                
                }
                
                if (strlen (actual_type) == 1 && strlen (formal_type) == 1
                    && actual_type[0] != formal_type[0] && actual_type[0] != 'N') {
                
                    char local_name[64], source[64], operation[4];
                    
                    sprintf (local_name, "__j2d_call_widen_%lu", method->nb_locals);
                    sprintf (source, "$%lu", index);
                    sprintf (operation, "!%c", formal_type[0]);
                    
                    if (add_local_info (method, local_name, formal_type, &converted_index)) {
                    
                        free (converted_args);
                        
                        free (actual_type);
                        free (formal_type);
                        
                        goto fail;
                    
                    }
                    
                    add_local_statement (method, converted_index, ARG_ARITHMETIC, source, operation);
                
                }
                
                if (!first_arg) {
                    append_text (&converted_args, &converted_len, ",");
                }
                
                sprintf (tmp, "%lu", converted_index);
                append_text (&converted_args, &converted_len, tmp);
                
                first_arg = 0;
                
                actual += strlen (actual_type);
                formal += strlen (formal_type);
                
                arg = *end == ',' ? end + 1 : end;
                
                free (actual_type);
                free (formal_type);
            
            }
            
            free (args);
            args = converted_args;
            
            free (params);
            params = xstrdup (resolved);
        
        }
    
    }
    
    *encoded_out = xmalloc (strlen (params) + strlen (args) + 2);
    sprintf (*encoded_out, "%s|%s", params, args);
    
    free (params);
    free (args);
    
    return 0;

fail:

    free (params);
    free (args);
    
    return 1;
}

static int parse_own_call_arguments (struct source_names *names, struct method_info *method, char **encoded_out, const char *called_name) {

    char *type = dex_class_descriptor (names);
    int ret = parse_call_arguments (names, method, encoded_out, type, called_name);
    
    free (type);
    return ret;

}

static int parse_target_call_arguments (struct source_names *names, struct method_info *method, char **encoded_out, const char *target, const char *called_name) {

    char *type;
    int ret;
    
    if (target && target[0] == '$') {
    
        unsigned long index = strtoul (target + 1, 0, 10);
        type = index < method->nb_locals ? xstrdup (method->locals[index].type) : 0;
    
    } else {
        type = target ? resolve_class_type (names, target) : dex_class_descriptor (names);
    }
    
    ret = parse_call_arguments (names, method, encoded_out, type, called_name);
    
    free (type);
    return ret;

}

static int parse_local_array_initializer (struct source_names *names, struct method_info *method, const char *type, const char *name) {

    unsigned long array_local, size_local, index_local;
    unsigned long size_statement, count = 0;
    
    char local_name[64], source[64], literal[64];
    
    if (type[0] != '[' || type[1] == '[') {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "array initializer requires a one-dimensional array type");
        return 1;
    
    }
    
    sprintf (local_name, "__j2d_array_size_%lu", method->nb_locals);
    
    if (add_local_info (method, local_name, "I", &size_local)) {
        return 1;
    }
    
    size_statement = method->nb_statements;
    add_local_statement (method, size_local, ARG_INT, 0, "0");
    
    if (add_local_info (method, name, type, &array_local)) {
        return 1;
    }
    
    sprintf (source, "$%lu", size_local);
    add_local_statement (method, array_local, ARG_NEW_ARRAY, source, type);
    
    sprintf (local_name, "__j2d_array_index_%lu", method->nb_locals);
    
    if (add_local_info (method, local_name, "I", &index_local)) {
        return 1;
    }
    
    get_token ();
    
    while (tok.kind != TOK_RBRACE) {
    
        struct field_mul_expr expr;
        const char *actual_type;
        
        unsigned long value_local, converted_local;
        
        char conversion[3];
        int compatible = 0;
        
        if (tok.kind == TOK_EOF) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "expected '}' in array initializer");
            return 1;
        
        }
        
        if (parse_body_expression (names, method, &expr)) {
            return 1;
        }
        
        actual_type = expression_argument_type (names, method, &expr);
        
        if (expr.type == 'N' && (type[1] == 'L' || type[1] == '[')) {
            compatible = 1;
        } else if (actual_type && (type[1] == 'L' || type[1] == '[')) {
            compatible = declared_reference_assignable (actual_type, type + 1);
        } else if (actual_type) {
            compatible = primitive_widening_assignable (actual_type[0], type[1]);
        }
        
        if (!compatible && expr.is_constant && expr.type == 'I') {
        
            if (type[1] == 'B') {
                compatible = expr.constant >= -128 && expr.constant <= 127;
            } else if (type[1] == 'S') {
                compatible = expr.constant >= -32768 && expr.constant <= 32767;
            } else if (type[1] == 'C') {
                compatible = expr.constant >= 0 && expr.constant <= 65535;
            }
        
        }
        
        if (!compatible) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "incompatible type in local array initializer");
                
            free_field_mul_expr (&expr);
            return 1;
        
        }
        
        sprintf (literal, "%lu", count);
        add_local_statement (method, index_local, ARG_INT, 0, literal);
        
        if (expr.type == 'N') {
        
            sprintf (local_name, "__j2d_array_value_%lu", method->nb_locals);
            
            if (add_local_info (method, local_name, type + 1, &value_local)) {
            
                free_field_mul_expr (&expr);
                return 1;
            
            }
            
            add_local_statement (method, value_local, ARG_INT, 0, "0");
        
        } else if (add_constructor_argument_local (names, method, &expr, &value_local)) {
        
            free_field_mul_expr (&expr);
            return 1;
        
        }
        
        if (strcmp (method->locals[value_local].type, type + 1) != 0) {
        
            sprintf (local_name, "__j2d_array_value_%lu", method->nb_locals);
            
            if (add_local_info (method, local_name, type + 1, &converted_local)) {
            
                free_field_mul_expr (&expr);
                return 1;
            
            }
            
            sprintf (source, "$%lu", value_local);
            
            conversion[0] = '!';
            conversion[1] = type[1];
            conversion[2] = '\0';
            
            add_local_statement (method, converted_local, ARG_ARITHMETIC, source,
                type[1] == 'L' || type[1] == '[' ? "" : conversion);
            value_local = converted_local;
        
        }
        
        add_array_put_statement (method, array_local, index_local, value_local);
        
        free_field_mul_expr (&expr);
        count++;
        
        if (tok.kind == TOK_COMMA) {
        
            get_token ();
            
            if (tok.kind == TOK_RBRACE) {
                break;
            }
        
        } else if (tok.kind != TOK_RBRACE) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "expected ',' or '}' in array initializer");
            return 1;
        
        }
    
    }
    
    sprintf (literal, "%lu", count);
    
    free (method->statements[size_statement].arg_text);
    method->statements[size_statement].arg_text = xstrdup (literal);
    
    get_token ();
    
    if (tok.kind != TOK_SEMI && tok.kind != TOK_COMMA) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "expected ';' or ',' after local array initializer");
        return 1;
    
    }
    
    return 0;

}

static int parse_local_declarator (struct source_names *names, struct method_info *method, char *type, char *name) {

    unsigned long local_index;
    struct field_mul_expr expr;
    
    const char *init_filename = 0;
    unsigned long init_line = 0;
    
    const char *init_start = 0;
    const char *init_caret = 0;
    
    if (tok.kind == TOK_SEMI || tok.kind == TOK_COMMA) {
    
        if (add_local_info (method, name, type, &local_index)) {
        
            free (type);
            free (name);
            
            return 1;
        
        }
        
        free (type);
        free (name);
        
        return 0;
    
    }
    
    if (expect_token (TOK_ASSIGN, "'=' in local variable declaration")) {
    
        free (type);
        free (name);
        
        return 1;
    
    }
    
    get_token ();
    
    init_filename = xstrdup (get_filename ());
    init_line = get_line_number ();
    
    init_start = xstrdup (tok.start);
    init_caret = init_start + (tok.caret - tok.start);
    
    if (tok.kind == TOK_LBRACE) {
    
        int ret = parse_local_array_initializer (names, method, type, name);
        
        free ((char *) init_filename);
        free ((char *) init_start);
        
        free (type);
        free (name);
        
        return ret;
    
    }
    
    if (tok.kind == TOK_NEW) {
    
        unsigned long ctor_args_len = 0;
        char *ctor_args = 0;
        
        unsigned long ctor_params_len = 0;
        char *ctor_params = 0;
        
        unsigned long constructor_caret_offset = 0;
        int first_arg = 1;
        
        char *new_type = 0;
        char *new_name = 0;
        
        get_token ();
        
        if (tok.kind == TOK_BYTE || tok.kind == TOK_CHAR
            || tok.kind == TOK_SHORT || tok.kind == TOK_INT
                || tok.kind == TOK_LONG || tok.kind == TOK_FLOAT
                    || tok.kind == TOK_DOUBLE) {
        
            char base_desc[2];
            char *array_desc;
            
            unsigned long size_local;
            
            struct field_mul_expr size_expr;
            char size_source[64];
            
            switch (tok.kind) {
            
                case TOK_BYTE:
                
                    base_desc[0] = 'B';
                    break;
                
                case TOK_CHAR:
                
                    base_desc[0] = 'C';
                    break;
                
                case TOK_SHORT:
                
                    base_desc[0] = 'S';
                    break;
                
                case TOK_INT:
                
                    base_desc[0] = 'I';
                    break;
                
                case TOK_LONG:
                
                    base_desc[0] = 'J';
                    break;
                
                case TOK_FLOAT:
                
                    base_desc[0] = 'F';
                    break;
                
                default:
                
                    base_desc[0] = 'D';
                    break;
            
            }
            
            base_desc[1] = '\0';
            get_token ();
            
            if (expect_token (TOK_LBRACK, "'[' in array allocation")) {
            
                free (type);
                free (name);
                
                return 1;
            
            }
            
            get_token ();
            
            if (parse_body_expression (names, method, &size_expr)) {
            
                free (type);
                free (name);
                
                return 1;
            
            }
            
            if (expect_token (TOK_RBRACK, "']' after array size")) {
            
                free_field_mul_expr (&size_expr);
                
                free (type);
                free (name);
                
                return 1;
            
            }
            
            get_token ();
            
            if (tok.kind != TOK_SEMI && tok.kind != TOK_COMMA) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "expected ';' or ',' after array declaration");
                
                free_field_mul_expr (&size_expr);
                
                free (type);
                free (name);
                
                return 1;
            
            }
            
            array_desc = xmalloc (3);
            array_desc[0] = '[';
            array_desc[1] = base_desc[0];
            array_desc[2] = '\0';
            
            if (strcmp (type, array_desc) != 0) {
            
                report_line_at (get_filename (), init_line, REPORT_ERROR, init_start, init_caret, "incompatible array type in initializer");
                
                free_field_mul_expr (&size_expr);
                free (array_desc);
                free (type);
                free (name);
                
                return 1;
            
            }
            
            if (add_constructor_argument_local (names, method, &size_expr, &size_local) || add_local_info (method, name, type, &local_index)) {
            
                free_field_mul_expr (&size_expr);
                free (array_desc);
                free (type);
                free (name);
                
                return 1;
            
            }
            
            sprintf (size_source, "$%lu", size_local);
            add_local_statement (method, local_index, ARG_NEW_ARRAY, size_source, array_desc);
            
            free_field_mul_expr (&size_expr);
            free (array_desc);
            free (type);
            free (name);
            
            return 0;
        
        }
        
        if (expect_ident ("class name after 'new'")) {
        
            free (type);
            free (name);
            
            return 1;
        
        }
        
        new_name = xstrdup (tok.ident);
        
        constructor_caret_offset = (unsigned long) (tok.caret - tok.start);
        new_type = resolve_class_type (names, new_name);
        
        get_token ();
        
        if (expect_token (TOK_LPAREN, "'(' after class name")) {
        
            free (new_type);
            free (new_name);
            
            free (type);
            free (name);
            
            return 1;
        
        }
        
        get_token ();
        
        while (tok.kind != TOK_RPAREN) {
        
            struct field_mul_expr arg_expr;
            unsigned long arg_local;
            
            const char *arg_type;
            char tmp[64];
            
            if (!first_arg) {
            
                if (expect_token (TOK_COMMA, "',' between constructor arguments")) {
                
                    free (ctor_params);
                    free (ctor_args);
                    
                    free (new_type);
                    free (new_name);
                    
                    free (type);
                    free (name);
                    
                    return 1;
                
                }
                
                get_token ();
            
            }
            
            if (tok.kind == TOK_PPSTR) {
            
                char local_name[64];
                char *literal = copy_string_literal (tok.ident);
                
                sprintf (local_name, "__j2d_ctor_arg_%lu", method->nb_locals);
                
                const char *literal_type = declared_reference_call_parameter (new_type, "<init>");
                
                if (!literal_type) {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret,
                        "cannot resolve a unique constructor parameter type from source declarations");
                    
                    free (new_type);
                    free (new_name);
                    
                    free (literal);
                    free (type);
                    free (name);
                    
                    return 1;
                
                }
                
                if (add_local_info (method, local_name, literal_type, &arg_local)) {
                
                    free (ctor_params);
                    free (ctor_args);
                    
                    free (new_type);
                    free (new_name);
                    
                    free (type);
                    free (name);
                    
                    free (literal);
                    return 1;
                
                }
                
                add_local_statement (method, arg_local, ARG_STRING, 0, literal);
                free (literal);
                
                append_text (&ctor_params, &ctor_params_len, literal_type);
                
                if (!first_arg) {
                    append_text (&ctor_args, &ctor_args_len, ",");
                }
                
                sprintf (tmp, "%lu", arg_local);
                append_text (&ctor_args, &ctor_args_len, tmp);
                
                get_token ();
                first_arg = 0;
                
                if (tok.kind != TOK_COMMA && tok.kind != TOK_RPAREN) {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected ',' or ')' after constructor argument");
                    
                    free (ctor_params);
                    free (ctor_args);
                    
                    free (new_type);
                    free (new_name);
                    
                    free (type);
                    free (name);
                    
                    return 1;
                
                }
                
                continue;
            
            }
            
            if (parse_body_expression (names, method, &arg_expr)) {
            
                free (ctor_params);
                free (ctor_args);
                
                free (new_type);
                free (new_name);
                
                free (type);
                free (name);
                
                return 1;
            
            }
            
            arg_type = expression_type_descriptor (arg_expr.type);
            
            if (!arg_type || add_constructor_argument_local (names, method, &arg_expr, &arg_local)) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unsupported constructor argument type");
                free_field_mul_expr (&arg_expr);
                
                free (ctor_params);
                free (ctor_args);
                
                free (new_type);
                free (new_name);
                
                free (type);
                free (name);
                
                return 1;
            
            }
            
            append_text (&ctor_params, &ctor_params_len, arg_type);
            
            if (!first_arg) {
                append_text (&ctor_args, &ctor_args_len, ",");
            }
            
            sprintf (tmp, "%lu", arg_local);
            
            append_text (&ctor_args, &ctor_args_len, tmp);
            free_field_mul_expr (&arg_expr);
            
            first_arg = 0;
            
            if (tok.kind != TOK_COMMA && tok.kind != TOK_RPAREN) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected ',' or ')' after constructor argument");
                
                free (ctor_params);
                free (ctor_args);
                free (new_type);
                free (new_name);
                free (type);
                free (name);
                
                return 1;
            
            }
        
        }
        
        if (!ctor_params) {
            ctor_params = xstrdup ("");
        }
        
        {
        
            char *encoded = 0;
            unsigned long encoded_len = 0;
            
            append_text (&encoded, &encoded_len, ctor_params);
            append_text (&encoded, &encoded_len, "|");
            
            if (ctor_args) {
                append_text (&encoded, &encoded_len, ctor_args);
            }
            
            free (ctor_args);
            free (ctor_params);
            
            ctor_args = encoded;
        
        }
        
        get_token ();
        
        if (tok.kind != TOK_SEMI && tok.kind != TOK_COMMA) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "expected ';' or ',' after local variable declaration");
            
            free (ctor_args);
            free (new_type);
            free (new_name);
            free (type);
            free (name);
            
            return 1;
        
        }
        
        if (strcmp (type, new_type) != 0) {
        
            report_line_at (get_filename (), init_line, REPORT_ERROR, init_start, init_caret, "incompatible types in object initializer");
            
            free (ctor_args);
            free (new_type);
            free (new_name);
            free (type);
            free (name);
            
            return 1;
        
        }
        
        if (add_local_info (method, name, type, &local_index)) {
        
            free (ctor_args);
            free (new_type);
            free (new_name);
            free (type);
            free (name);
            
            return 1;
        
        }
        
        add_local_statement (method, local_index, ARG_NEW, new_name, ctor_args);
        free (ctor_args);
        
        method->statements[method->nb_statements - 1].filename = init_filename;
        method->statements[method->nb_statements - 1].line = init_line;
        method->statements[method->nb_statements - 1].start = init_start;
        method->statements[method->nb_statements - 1].caret = init_start + constructor_caret_offset;
        
        free (new_type);
        free (new_name);
        free (type);
        free (name);
        
        return 0;
    
    }
    
    if (parse_body_expression (names, method, &expr)) {
    
        /* Retain the declared local for subsequent diagnostics. */
        add_local_info (method, name, type, &local_index);
        
        free (type);
        free (name);
        
        return 1;
    
    }
    
    if (expr.has_field && field_initializer_is_call (expr.field)) {
    
        char *call_target = 0;
        char *call_method = 0;
        
        if (!split_field_call_initializer (expr.field, &call_target, &call_method)) {
        
            const char *call_caret = strstr (init_start, call_target);
            
            if (call_caret) {
                init_caret = call_caret;
            }
            
            free (call_target);
            free (call_method);
        
        }
    
    }
    
    if (tok.kind != TOK_SEMI && tok.kind != TOK_COMMA) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "expected ';' or ',' after local variable declaration");
        
        free_field_mul_expr (&expr);
        
        free (type);
        free (name);
        
        return 1;
    
    }
    
    if ((type[0] == 'I' || type[0] == 'S' || type[0] == 'J'
        || type[0] == 'B' || type[0] == 'C')
            && (expr.type == 'D' || expr.type == 'F')) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "incompatible types: possible lossy conversion in local variable initializer");
        
        free_field_mul_expr (&expr);
        free (type);
        free (name);
        
        return 1;
    
    }
    
    if (add_local_info (method, name, type, &local_index)) {
    
        free_field_mul_expr (&expr);
        
        free (type);
        free (name);
        
        return 1;
    
    }
    
    if (!expr.has_field) {
    
        const char *literal = expr.literal_text;
        char value[64];
        
        if (!literal) {
        
            sprintf (value, "%ld", expr.constant);
            literal = value;
        
        }
        
        add_local_statement (method, local_index, ARG_INT, 0, literal);
    
    } else {
    
        if (!expr.factors) {
            expr.factors = xstrdup ("1");
        }
        
        add_local_statement (method, local_index, ARG_ARITHMETIC, expr.field, expr.factors);
    
    }
    
    method->statements[method->nb_statements - 1].filename = init_filename;
    method->statements[method->nb_statements - 1].line = init_line;
    
    method->statements[method->nb_statements - 1].start = init_start;
    method->statements[method->nb_statements - 1].caret = init_caret;
    
    free_field_mul_expr (&expr);
    
    free (type);
    free (name);
    
    return 0;

}

static int parse_local_declaration (struct source_names *names, struct method_info *method) {

    char *declarator_base_type = 0;
    
    if (parse_type_name (names, 0, &declarator_base_type)) {
        return 1;
    }
    
    for (;;) {
    
        char *name, *type;
        unsigned long array_depth = 0;
        
        if (expect_ident ("local variable name")) {
        
            free (declarator_base_type);
            return 1;
        
        }
        
        name = xstrdup (tok.ident);
        
        if (find_local_info_by_name (method, name, 0)) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "local variable '%s' is already defined", name);
            
            free (declarator_base_type);
            free (name);
            
            return 1;
        
        }
        
        get_token ();
        
        if (append_array_suffix (&array_depth)) {
        
            free (declarator_base_type);
            free (name);
            
            return 1;
        
        }
        
        type = apply_array_suffix (xstrdup (declarator_base_type), array_depth);
        
        if (parse_local_declarator (names, method, type, name)) {
        
            free (declarator_base_type);
            return 1;
        
        }
        
        if (tok.kind != TOK_COMMA) {
            break;
        }
        
        get_token ();
    
    }
    
    if (expect_token (TOK_SEMI, "';' after local variable declaration")) {
    
        free (declarator_base_type);
        return 1;
    
    }
    
    free (declarator_base_type);
    return 0;

}

static int token_starts_reference_local_declaration (void) {

    const char *p;
    
    if (tok.kind != TOK_IDENT || !tok.caret) {
        return 0;
    }
    
    p = tok.caret + tok.len;
    
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    
    while (*p == '[') {
    
        p++;
        
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        
        if (*p != ']') {
            return 0;
        }
        
        p++;
        
        while (*p == ' ' || *p == '\t') {
            p++;
        }
    
    }
    
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_' || *p == '$')) {
        return 0;
    }
    
    p++;
    
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')
        || (*p >= '0' && *p <= '9') || *p == '_' || *p == '$') {
        p++;
    }
    
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    
    while (*p == '[') {
    
        p++;
        
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        
        if (*p != ']') {
            return 0;
        }
        
        p++;
        
        while (*p == ' ' || *p == '\t') {
            p++;
        }
    
    }
    
    return *p == '=' || *p == ',' || *p == ';';

}

static struct statement_info *append_control_statement (struct method_info *method, int kind) {

    struct statement_info *stmt;
    
    method->statements = xrealloc (method->statements,
        (method->nb_statements + 1) * sizeof (*method->statements));
    stmt = &method->statements[method->nb_statements++];
    
    memset (stmt, 0, sizeof (*stmt));
    stmt->kind = kind;
    
    return stmt;

}

static void add_try_info (struct method_info *method, unsigned long start_stmt, unsigned long end_stmt, unsigned long handler_stmt, const char *type) {

    struct try_info *info;
    
    method->tries = xrealloc (method->tries, (method->nb_tries + 1) * sizeof (*method->tries));
    info = &method->tries[method->nb_tries++];
    
    info->start_stmt = start_stmt;
    info->end_stmt = end_stmt;
    info->handler_stmt = handler_stmt;
    info->type = type ? xstrdup (type) : 0;

}

static int parse_goto_statement (struct method_info *method) {

    struct statement_info *stmt;
    const char *start, *caret;
    
    unsigned long line;
    char *name;
    
    start = xstrdup (tok.start);
    caret = start + (tok.caret - tok.start);
    
    line = get_line_number ();
    get_token ();
    
    if (expect_ident ("label name after goto")) {
    
        free ((char *) start);
        return 1;
    
    }
    
    name = xstrdup (tok.ident);
    get_token ();
    
    if (expect_token (TOK_SEMI, "';' after goto statement")) {
    
        free ((char *) start);
        free (name);
        
        return 1;
    
    }
    
    stmt = append_control_statement (method, STMT_GOTO);
    stmt->target = name;
    
    stmt->filename = xstrdup (get_filename ());
    stmt->line = line;
    
    stmt->start = start;
    stmt->caret = caret;
    
    return 0;

}

static int resolve_named_gotos (struct method_info *method) {

    unsigned long i, j;
    
    for (i = 0; i < method->nb_statements; i++) {
    
        struct statement_info *stmt = &method->statements[i];
        
        if (stmt->kind != STMT_GOTO || !stmt->target) {
            continue;
        }
        
        for (j = 0; j < method->nb_statements; j++) {
        
            if (method->statements[j].kind == STMT_LABEL
                && strcmp (method->statements[j].target, stmt->target) == 0) {
                stmt->jump_target = j;
                break;
            }
        }
        
        if (j == method->nb_statements) {
        
            report_line_at (stmt->filename, stmt->line, REPORT_ERROR,
                stmt->start, stmt->caret, "unknown label '%s'", stmt->target);
            return 1;
        }
    }
    
    return 0;
}

static int parse_block_statements (struct source_names *names, struct method_info *method);
static int parse_while_statement (struct source_names *names, struct method_info *method);
static int parse_do_statement (struct source_names *names, struct method_info *method);
static int parse_for_statement (struct source_names *names, struct method_info *method);
static int parse_if_statement (struct source_names *names, struct method_info *method);

static int parse_switch_statement (struct source_names *names, struct method_info *method);
static int parse_try_statement (struct source_names *names, struct method_info *method);

struct break_context {

    unsigned long *jumps;
    unsigned long nb_jumps;
    
    struct break_context *previous;

};

static struct break_context *current_break_context;

struct continue_context {

    unsigned long *jumps;
    unsigned long nb_jumps;
    
    struct continue_context *previous;

};

static struct continue_context *current_continue_context;

static int parse_continue_statement (struct method_info *method) {

    unsigned long jump;
    
    if (!current_continue_context) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "continue statement not within loop");
        return 1;
    
    }
    
    get_token ();
    
    if (expect_token (TOK_SEMI, "';' after continue")) {
        return 1;
    }
    
    jump = method->nb_statements;
    append_control_statement (method, STMT_GOTO);
    
    current_continue_context->jumps = xrealloc (current_continue_context->jumps,
        (current_continue_context->nb_jumps + 1) * sizeof (*current_continue_context->jumps));
    current_continue_context->jumps[current_continue_context->nb_jumps++] = jump;
    
    return 0;

}

static int parse_break_statement (struct method_info *method) {

    unsigned long jump;
    
    if (!current_break_context) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "break statement not within switch");
        return 1;
    
    }
    
    get_token ();
    
    if (expect_token (TOK_SEMI, "';' after break")) {
        return 1;
    }
    
    jump = method->nb_statements;
    append_control_statement (method, STMT_GOTO);
    
    current_break_context->jumps = xrealloc (current_break_context->jumps,
        (current_break_context->nb_jumps + 1) * sizeof (*current_break_context->jumps));
    current_break_context->jumps[current_break_context->nb_jumps++] = jump;
    
    return 0;

}

static int parse_switch_statement_inner (struct source_names *names, struct method_info *method) {

    struct field_mul_expr expression;
    struct break_context break_context;
    struct statement_info *stmt;
    
    unsigned long initial_jump;
    unsigned long dispatch_start;
    unsigned long default_target;
    unsigned long end_label;
    unsigned long i;
    
    struct {
    
        long value;
        unsigned long target;
    
    } *cases = 0;
    
    unsigned long nb_cases = 0;
    get_token ();
    
    if (expect_token (TOK_LPAREN, "'(' after switch")) {
        return 1;
    }
    
    get_token ();
    memset (&expression, 0, sizeof (expression));
    
    if (parse_body_expression (names, method, &expression)) {
        return 1;
    }
    
    if (expect_token (TOK_RPAREN, "')' after switch expression")) {
    
        free_field_mul_expr (&expression);
        return 1;
    
    }
    
    if (!expression.has_field) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "switch expression must reference a variable or field");
        free_field_mul_expr (&expression);
        
        return 1;
    
    }
    
    get_token ();
    
    if (expect_token (TOK_LBRACE, "'{' after switch")) {
    
        free_field_mul_expr (&expression);
        return 1;
    
    }
    
    initial_jump = method->nb_statements;
    append_control_statement (method, STMT_GOTO);
    
    default_target = (unsigned long) -1;
    
    memset (&break_context, 0, sizeof (break_context));
    break_context.previous = current_break_context;
    
    current_break_context = &break_context;
    get_token ();
    
    while (tok.kind != TOK_RBRACE) {
    
        if (tok.kind == TOK_EOF) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "unexpected end of file in switch");
            current_break_context = break_context.previous;
            
            free (break_context.jumps);
            free (cases);
            
            free_field_mul_expr (&expression);
            return 1;
        
        }
        
        if (tok.kind == TOK_CASE) {
        
            int negative = 0;
            long value;
            
            get_token ();
            
            if (tok.kind == TOK_MINUS) {
            
                negative = 1;
                get_token ();
            
            }
            
            if (tok.kind != TOK_CINT && tok.kind != TOK_CLONG && tok.kind != TOK_CCHAR) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "expected integer constant after case");
                current_break_context = break_context.previous;
                
                free (break_context.jumps);
                free (cases);
                
                free_field_mul_expr (&expression);
                return 1;
            
            }
            
            value = (tok.kind == TOK_CCHAR) ? (long) tok.val.i.low : strtol (tok.ident, 0, 0);
            
            if (negative) {
                value = -value;
            }
            
            get_token ();
            
            if (expect_token (TOK_COLON, "':' after case value")) {
            
                current_break_context = break_context.previous;
                
                free (break_context.jumps);
                free (cases);
                
                free_field_mul_expr (&expression);
                return 1;
            
            }
            
            for (i = 0; i < nb_cases; i++) {
            
                if (cases[i].value == value) {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                        tok.start, tok.caret, "duplicate case value");
                    current_break_context = break_context.previous;
                    
                    free (break_context.jumps);
                    free (cases);
                    
                    free_field_mul_expr (&expression);
                    return 1;
                
                }
            
            }
            
            append_control_statement (method, STMT_LABEL);
            
            cases = xrealloc (cases, (nb_cases + 1) * sizeof (*cases));
            cases[nb_cases].value = value;
            cases[nb_cases].target = method->nb_statements - 1;
            
            nb_cases++;
            get_token ();
            
            continue;
        
        }
        
        if (tok.kind == TOK_DEFAULT) {
        
            if (default_target != (unsigned long) -1) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "duplicate default label");
                current_break_context = break_context.previous;
                
                free (break_context.jumps);
                free (cases);
                
                free_field_mul_expr (&expression);
                return 1;
            
            }
            
            get_token ();
            
            if (expect_token (TOK_COLON, "':' after default")) {
            
                current_break_context = break_context.previous;
                
                free (break_context.jumps);
                free (cases);
                
                free_field_mul_expr (&expression);
                return 1;
            
            }
            
            append_control_statement (method, STMT_LABEL);
            
            default_target = method->nb_statements - 1;
            get_token ();
            
            continue;
        
        }
        
        if (tok.kind == TOK_CONTINUE) {
        
            if (parse_continue_statement (method)) {
                goto switch_error;
            }
            
            get_token ();
            continue;
        
        }
        
        if (tok.kind == TOK_BREAK) {
        
            if (parse_break_statement (method)) {
            
                current_break_context = break_context.previous;
                
                free (break_context.jumps);
                free (cases);
                
                free_field_mul_expr (&expression);
                return 1;
            
            }
            
            get_token ();
            continue;
        
        }
        
        if (tok.kind == TOK_SWITCH) {
        
            if (parse_switch_statement (names, method)) {
            
                current_break_context = break_context.previous;
                
                free (break_context.jumps);
                free (cases);
                
                free_field_mul_expr (&expression);
                return 1;
            
            }
            
            continue;
        
        }
        
        if (tok.kind == TOK_IF) {
        
            if (parse_if_statement (names, method)) {
                goto switch_error;
            }
            
            continue;
        
        } else if (tok.kind == TOK_FOR) {
        
            if (parse_for_statement (names, method)) {
                goto switch_error;
            }
            
            continue;
        
        } else if (tok.kind == TOK_WHILE) {
        
            if (parse_while_statement (names, method)) {
                goto switch_error;
            }
            
            continue;
        
        } else if (tok.kind == TOK_SWITCH) {
        
            if (parse_switch_statement (names, method)) {
                return 1;
            }
            
            continue;
        
        } else if (tok.kind == TOK_TRY) {
        
            if (parse_try_statement (names, method)) {
                goto switch_error;
            }
            
            continue;
        
        } else if (tok.kind == TOK_RETURN) {
        
            if (parse_return_statement (names, method)) {
                goto switch_error;
            }
        
        } else if (tok.kind == TOK_THROW) {
        
            if (parse_throw_statement (names, method)) {
                goto switch_error;
            }
        
        } else if (tok.kind == TOK_GOTO) {
        
            if (parse_goto_statement (method)) {
                goto switch_error;
            }
        
        } else if (tok.kind == TOK_BYTE || tok.kind == TOK_CHAR
            || tok.kind == TOK_SHORT || tok.kind == TOK_INT
                || tok.kind == TOK_LONG || tok.kind == TOK_FLOAT
                    || tok.kind == TOK_DOUBLE || tok.kind == TOK_BOOLEAN
                        || token_starts_reference_local_declaration ()) {
        
            if (parse_local_declaration (names, method)) {
                goto switch_error;
            }
        
        } else {
        
            if (parse_statement (names, method)) {
                goto switch_error;
            }
        
        }
        
        get_token ();
    
    }
    
    current_break_context = break_context.previous;
    end_label = method->nb_statements;
    
    append_control_statement (method, STMT_GOTO);
    dispatch_start = method->nb_statements;
    
    method->statements[initial_jump].jump_target = dispatch_start;
    
    for (i = 0; i < nb_cases; i++) {
    
        stmt = append_control_statement (method, STMT_IF_EQ_CONST);
        stmt->target = xstrdup (expression.field);
        
        stmt->arg_text = xstrdup (expression.factors ? expression.factors : "1");
        stmt->arg_param_index = (unsigned long) cases[i].value;
        
        stmt->jump_target = cases[i].target;
    
    }
    
    append_control_statement (method, STMT_GOTO)->jump_target = default_target != (unsigned long) -1 ? default_target : method->nb_statements + 1;
    
    end_label = method->nb_statements;
    append_control_statement (method, STMT_LABEL);
    
    method->statements[dispatch_start - 1].jump_target = end_label;
    
    for (i = 0; i < break_context.nb_jumps; i++) {
        method->statements[break_context.jumps[i]].jump_target = end_label;
    }
    
    free (break_context.jumps);
    free (cases);
    free_field_mul_expr (&expression);
    
    get_token ();
    return 0;
    
switch_error:
    
    current_break_context = break_context.previous;
    
    free (break_context.jumps);
    free (cases);
    
    free_field_mul_expr (&expression);
    return 1;

}

static int parse_switch_statement (struct source_names *names, struct method_info *method) {

    int ret;
    
    method->local_scope_depth++;
    ret = parse_switch_statement_inner (names, method);
    
    leave_local_scope (method);
    return ret;

}

static int parse_try_statement (struct source_names *names, struct method_info *method) {

    unsigned long try_start, try_end, normal_jump;
    unsigned long *handler_jumps = 0, nb_handler_jumps = 0;
    unsigned long finally_start = 0, end_stmt;
    int have_catch = 0, have_finally = 0;
    
    get_token ();
    
    if (expect_token (TOK_LBRACE, "'{' after try")) {
        return 1;
    }
    
    try_start = method->nb_statements;
    method->try_depth++;
    
    get_token ();
    
    if (parse_block_statements (names, method)) {
        return 1;
    }
    
    method->try_depth--;
    try_end = method->nb_statements;
    
    normal_jump = method->nb_statements;
    append_control_statement (method, STMT_GOTO);
    
    get_token ();
    
    while (tok.kind == TOK_CATCH) {
    
        char *catch_name, *catch_type;
        
        unsigned long local_index, handler_start;
        unsigned long catch_jump;
        
        have_catch = 1;
        get_token ();
        
        if (expect_token (TOK_LPAREN, "'(' after catch")) {
            return 1;
        }
        
        get_token ();
        
        if (parse_type_name (names, 0, &catch_type)) {
            return 1;
        }
        
        if (expect_ident ("exception variable in catch")) {
        
            free (catch_type);
            return 1;
        
        }
        
        catch_name = xstrdup (tok.ident);
        
        if (add_local_info (method, catch_name, catch_type, &local_index)) {
        
            free (catch_name);
            free (catch_type);
            
            return 1;
        
        }
        
        method->locals[local_index].scope_depth++;
        free (catch_name);
        
        get_token ();
        
        if (expect_token (TOK_RPAREN, "')' after catch parameter")) {
        
            free (catch_type);
            return 1;
        
        }
        
        get_token ();
        
        if (expect_token (TOK_LBRACE, "'{' after catch parameter")) {
        
            free (catch_type);
            return 1;
        
        }
        
        handler_start = method->nb_statements;
        
        {
        
            struct statement_info *stmt;
            
            stmt = append_control_statement (method, STMT_MOVE_EXCEPTION);
            stmt->target = xmalloc (32);
            sprintf (stmt->target, "$%lu", local_index);
        
        }
        
        add_try_info (method, try_start, try_end, handler_start, catch_type);
        free (catch_type);
        
        get_token ();
        
        if (parse_block_statements (names, method)) {
            return 1;
        }
        
        catch_jump = method->nb_statements;
        append_control_statement (method, STMT_GOTO);
        
        handler_jumps = xrealloc (handler_jumps, (nb_handler_jumps + 1) * sizeof (*handler_jumps));
        handler_jumps[nb_handler_jumps++] = catch_jump;
        
        get_token ();
    
    }
    
    if (tok.kind == TOK_FINALLY) {
    
        have_finally = 1;
        get_token ();
        
        if (expect_token (TOK_LBRACE, "'{' after finally")) {
        
            free (handler_jumps);
            return 1;
        
        }
        
        finally_start = method->nb_statements;
        get_token ();
        
        if (parse_block_statements (names, method)) {
        
            free (handler_jumps);
            return 1;
        
        }
        
        get_token ();
    
    }
    
    if (!have_catch && !have_finally) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "try must be followed by catch or finally");
        
        free (handler_jumps);
        return 1;
    
    }
    
    end_stmt = method->nb_statements;
    method->statements[normal_jump].jump_target = have_finally ? finally_start : end_stmt;
    
    while (nb_handler_jumps) {
    
        method->statements[handler_jumps[--nb_handler_jumps]].jump_target =
            have_finally ? finally_start : end_stmt;
    
    }
    
    free (handler_jumps);
    return 0;

}

static int parse_if_body (struct source_names *names, struct method_info *method) {

    if (tok.kind == TOK_LBRACE) {
    
        get_token ();
        
        if (parse_block_statements (names, method)) {
            return 1;
        }
        
        get_token ();
        return 0;
    
    }
    
    if (tok.kind == TOK_IF) {
        return parse_if_statement (names, method);
    }
    
    if (tok.kind == TOK_DO) {
        return parse_do_statement (names, method);
    }
    
    if (tok.kind == TOK_FOR) {
        return parse_for_statement (names, method);
    }
    
    if (tok.kind == TOK_WHILE) {
        return parse_while_statement (names, method);
    }
    
    if (tok.kind == TOK_SWITCH) {
        return parse_switch_statement (names, method);
    }
    
    if (tok.kind == TOK_TRY) {
        return parse_try_statement (names, method);
    }
    
    if (tok.kind == TOK_CONTINUE) {
    
        if (parse_continue_statement (method)) {
            return 1;
        }
    
    } else if (tok.kind == TOK_RETURN) {
    
        if (parse_return_statement (names, method)) {
            return 1;
        }
    
    } else if (tok.kind == TOK_THROW) {
    
        if (parse_throw_statement (names, method)) {
            return 1;
        }
    
    } else if (tok.kind == TOK_GOTO) {
    
        if (parse_goto_statement (method)) {
            return 1;
        }
    
    } else if (tok.kind == TOK_BYTE || tok.kind == TOK_CHAR
        || tok.kind == TOK_SHORT || tok.kind == TOK_INT
            || tok.kind == TOK_LONG || tok.kind == TOK_FLOAT
                || tok.kind == TOK_DOUBLE || tok.kind == TOK_BOOLEAN
                    || token_starts_reference_local_declaration ()) {
    
        if (parse_local_declaration (names, method)) {
            return 1;
        }
    
    } else {
    
        if (parse_statement (names, method)) {
            return 1;
        }
    
    }
    
    get_token ();
    return 0;

}

static int parse_if_statement (struct source_names *names, struct method_info *method) {

    struct field_mul_expr condition;
    struct statement_info *stmt;
    
    unsigned long false_jump;
    unsigned long end_jump;
    unsigned long false_label;
    unsigned long end_label;
    
    false_jump = (unsigned long) -1;
    end_jump = (unsigned long) -1;
    
    get_token ();
    
    if (expect_token (TOK_LPAREN, "'(' after if")) {
        return 1;
    }
    
    get_token ();
    memset (&condition, 0, sizeof (condition));
    
    if (parse_body_expression (names, method, &condition)) {
        return 1;
    }
    
    if (expect_token (TOK_RPAREN, "')' after if condition")) {
    
        free_field_mul_expr (&condition);
        return 1;
    
    }
    
    if (!condition.has_field) {
    
        if ((condition.literal_text ? strtod (condition.literal_text, 0) : (double) condition.constant) == 0.0) {
        
            false_jump = method->nb_statements;
            append_control_statement (method, STMT_GOTO);
        
        }
    
    } else {
    
        stmt = append_control_statement (method, STMT_IF_FALSE);
        
        stmt->target = xstrdup (condition.field);
        stmt->arg_text = xstrdup (condition.factors ? condition.factors : "1");
        
        false_jump = method->nb_statements - 1;
    
    }
    
    free_field_mul_expr (&condition);
    get_token ();
    
    if (parse_if_body (names, method)) {
        return 1;
    }
    
    if (tok.kind == TOK_ELSE) {
    
        end_jump = method->nb_statements;
        append_control_statement (method, STMT_GOTO);
        
        false_label = method->nb_statements;
        append_control_statement (method, STMT_LABEL);
        
        if (false_jump != (unsigned long) -1) {
            method->statements[false_jump].jump_target = false_label;
        }
        
        get_token ();
        
        if (parse_if_body (names, method)) {
            return 1;
        }
        
        end_label = method->nb_statements;
        
        append_control_statement (method, STMT_LABEL);
        method->statements[end_jump].jump_target = end_label;
    
    } else {
    
        false_label = method->nb_statements;
        append_control_statement (method, STMT_LABEL);
        
        if (false_jump != (unsigned long) -1) {
            method->statements[false_jump].jump_target = false_label;
        }
    
    }
    
    return 0;

}

static int parse_while_statement (struct source_names *names, struct method_info *method) {

    struct continue_context continue_context;
    struct field_mul_expr condition;
    struct statement_info *stmt;
    
    unsigned long loop_start;
    unsigned long condition_stmt;
    unsigned long end_label;
    unsigned long i;
    
    condition_stmt = (unsigned long) -1;
    loop_start = method->nb_statements;
    
    get_token ();
    
    if (expect_token (TOK_LPAREN, "'(' after while")) {
        return 1;
    }
    
    get_token ();
    memset (&condition, 0, sizeof (condition));
    
    if (parse_body_expression (names, method, &condition)) {
        return 1;
    }
    
    if (expect_token (TOK_RPAREN, "')' after while condition")) {
    
        free_field_mul_expr (&condition);
        return 1;
    
    }
    
    if (!condition.has_field) {
    
        if ((condition.literal_text ? strtod (condition.literal_text, 0) : (double) condition.constant) == 0.0) {
        
            condition_stmt = method->nb_statements;
            append_control_statement (method, STMT_GOTO);
        
        }
    
    } else {
    
        stmt = append_control_statement (method, STMT_IF_FALSE);
        
        stmt->target = xstrdup (condition.field);
        stmt->arg_text = xstrdup (condition.factors ? condition.factors : "1");
        
        condition_stmt = method->nb_statements - 1;
    
    }
    
    free_field_mul_expr (&condition);
    get_token ();
    
    memset (&continue_context, 0, sizeof (continue_context));
    
    continue_context.previous = current_continue_context;
    current_continue_context = &continue_context;
    
    if (parse_if_body (names, method)) {
    
        current_continue_context = continue_context.previous;
        
        free (continue_context.jumps);
        return 1;
    
    }
    
    current_continue_context = continue_context.previous;
    
    for (i = 0; i < continue_context.nb_jumps; i++) {
        method->statements[continue_context.jumps[i]].jump_target = loop_start;
    }
    
    free (continue_context.jumps);
    
    append_control_statement (method, STMT_GOTO)->jump_target = loop_start;
    end_label = method->nb_statements;
    
    append_control_statement (method, STMT_LABEL);
    
    if (condition_stmt != (unsigned long) -1) {
        method->statements[condition_stmt].jump_target = end_label;
    }
    
    return 0;

}

static int parse_do_statement (struct source_names *names, struct method_info *method) {

    struct continue_context continue_context;
    struct field_mul_expr condition;
    struct statement_info *stmt;
    
    unsigned long loop_start;
    unsigned long condition_start;
    
    unsigned long false_jump = (unsigned long) -1;
    unsigned long end_label;
    
    unsigned long i;
    
    loop_start = method->nb_statements;
    get_token ();
    
    memset (&continue_context, 0, sizeof (continue_context));
    
    continue_context.previous = current_continue_context;
    current_continue_context = &continue_context;
    
    if (parse_if_body (names, method)) {
    
        current_continue_context = continue_context.previous;
        
        free (continue_context.jumps);
        return 1;
    
    }
    
    current_continue_context = continue_context.previous;
    
    if (expect_token (TOK_WHILE, "'while' after do body")) {
    
        free (continue_context.jumps);
        return 1;
    
    }
    
    get_token ();
    
    if (expect_token (TOK_LPAREN, "'(' after while")) {
    
        free (continue_context.jumps);
        return 1;
    
    }
    
    get_token ();
    condition_start = method->nb_statements;
    
    for (i = 0; i < continue_context.nb_jumps; i++) {
        method->statements[continue_context.jumps[i]].jump_target = condition_start;
    }
    
    free (continue_context.jumps);
    memset (&condition, 0, sizeof (condition));
    
    if (parse_body_expression (names, method, &condition)) {
        return 1;
    }
    
    if (expect_token (TOK_RPAREN, "')' after do-while condition")) {
    
        free_field_mul_expr (&condition);
        return 1;
    
    }
    
    get_token ();
    
    if (expect_token (TOK_SEMI, "';' after do-while")) {
    
        free_field_mul_expr (&condition);
        return 1;
    
    }
    
    if (!condition.has_field) {
    
        if ((condition.literal_text ? strtod (condition.literal_text, 0) : (double) condition.constant) == 0.0) {
        
            false_jump = method->nb_statements;
            append_control_statement (method, STMT_GOTO);
        
        }
    
    } else {
    
        stmt = append_control_statement (method, STMT_IF_FALSE);
        
        stmt->target = xstrdup (condition.field);
        stmt->arg_text = xstrdup (condition.factors ? condition.factors : "1");
        
        false_jump = method->nb_statements - 1;
    
    }
    
    free_field_mul_expr (&condition);
    
    append_control_statement (method, STMT_GOTO)->jump_target = loop_start;
    end_label = method->nb_statements;
    
    append_control_statement (method, STMT_LABEL);
    
    if (false_jump != (unsigned long) -1) {
        method->statements[false_jump].jump_target = end_label;
    }
    
    get_token ();
    return 0;

}

static int parse_for_statement_inner (struct source_names *names, struct method_info *method) {

    struct continue_context continue_context;
    struct field_mul_expr condition;
    struct statement_info *saved_update;
    struct statement_info *stmt;
    
    unsigned long nb_saved_update;
    unsigned long update_start;
    unsigned long loop_start;
    unsigned long condition_stmt;
    unsigned long end_label;
    unsigned long i;
    
    int old_terminator;
    
    saved_update = 0;
    nb_saved_update = 0;
    condition_stmt = (unsigned long) -1;
    
    get_token ();
    
    if (expect_token (TOK_LPAREN, "'(' after for")) {
        return 1;
    }
    
    get_token ();
    
    if (tok.kind != TOK_SEMI) {
    
        if (tok.kind == TOK_BYTE || tok.kind == TOK_CHAR
            || tok.kind == TOK_SHORT || tok.kind == TOK_INT
                || tok.kind == TOK_LONG || tok.kind == TOK_FLOAT
                    || tok.kind == TOK_DOUBLE || tok.kind == TOK_BOOLEAN
                        || token_starts_reference_local_declaration ()) {
        
            if (parse_local_declaration (names, method)) {
                return 1;
            }
        
        } else {
        
            if (parse_statement (names, method)) {
                return 1;
            }
        
        }
    
    }
    
    if (expect_token (TOK_SEMI, "';' after for initializer")) {
        return 1;
    }
    
    get_token ();
    loop_start = method->nb_statements;
    
    if (tok.kind != TOK_SEMI) {
    
        memset (&condition, 0, sizeof (condition));
        
        if (parse_body_expression (names, method, &condition)) {
            return 1;
        }
        
        if (expect_token (TOK_SEMI, "';' after for condition")) {
        
            free_field_mul_expr (&condition);
            return 1;
        
        }
        
        if (!condition.has_field) {
        
            if ((condition.literal_text ? strtod (condition.literal_text, 0) : (double) condition.constant) == 0.0) {
            
                condition_stmt = method->nb_statements;
                append_control_statement (method, STMT_GOTO);
            
            }
        
        } else {
        
            stmt = append_control_statement (method, STMT_IF_FALSE);
            stmt->target = xstrdup (condition.field);
            stmt->arg_text = xstrdup (condition.factors ? condition.factors : "1");
            condition_stmt = method->nb_statements - 1;
        
        }
        
        free_field_mul_expr (&condition);
    
    }
    
    get_token ();
    
    if (tok.kind != TOK_RPAREN) {
    
        update_start = method->nb_statements;
        old_terminator = statement_terminator;
        statement_terminator = TOK_RPAREN;
        
        if (parse_statement (names, method)) {
        
            statement_terminator = old_terminator;
            return 1;
        
        }
        
        statement_terminator = old_terminator;
        nb_saved_update = method->nb_statements - update_start;
        
        if (nb_saved_update) {
        
            saved_update = xmalloc (nb_saved_update * sizeof (*saved_update));
            memcpy (saved_update, method->statements + update_start, nb_saved_update * sizeof (*saved_update));
            method->nb_statements = update_start;
        
        }
    
    }
    
    if (expect_token (TOK_RPAREN, "')' after for clauses")) {
    
        free (saved_update);
        return 1;
    
    }
    
    get_token ();
   
    if (expect_token (TOK_LBRACE, "'{' after for")) {
    
        free (saved_update);
        return 1;
    
    }
    
    get_token ();
    
    memset (&continue_context, 0, sizeof (continue_context));
    continue_context.previous = current_continue_context;
    
    current_continue_context = &continue_context;
    
    if (parse_block_statements (names, method)) {
    
        current_continue_context = continue_context.previous;
        
        free (continue_context.jumps);
        free (saved_update);
        
        return 1;
    
    }
    
    current_continue_context = continue_context.previous;
    update_start = method->nb_statements;
    
    for (i = 0; i < continue_context.nb_jumps; i++) {
        method->statements[continue_context.jumps[i]].jump_target = update_start;
    }
    
    free (continue_context.jumps);
    
    if (nb_saved_update) {
    
        method->statements = xrealloc (method->statements, (method->nb_statements + nb_saved_update) * sizeof (*method->statements));
        
        memcpy (method->statements + method->nb_statements, saved_update, nb_saved_update * sizeof (*saved_update));
        method->nb_statements += nb_saved_update;
        
        free (saved_update);
    
    }
    
    append_control_statement (method, STMT_GOTO)->jump_target = loop_start;
    end_label = method->nb_statements;
    
    append_control_statement (method, STMT_LABEL);
    
    if (condition_stmt != (unsigned long) -1) {
        method->statements[condition_stmt].jump_target = end_label;
    }
    
    get_token ();
    return 0;

}

static int parse_for_statement (struct source_names *names, struct method_info *method) {

    int ret;
    
    method->local_scope_depth++;
    ret = parse_for_statement_inner (names, method);
    
    leave_local_scope (method);
    return ret;

}

static int parse_block_statements_inner (struct source_names *names, struct method_info *method) {

    int depth = get_token_brace_depth ();
    int failed = 0;
    
    while (tok.kind != TOK_RBRACE) {
    
        if (tok.kind == TOK_EOF) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "unexpected end of file in method body");
            return 1;
        
        }
        
        if (tok.kind == TOK_IF) {
        
            if (parse_if_statement (names, method)) {
                return 1;
            }
            
            continue;
        
        } else if (tok.kind == TOK_FOR) {
        
            if (parse_for_statement (names, method)) {
                return 1;
            }
            
            continue;
        
        } else if (tok.kind == TOK_WHILE) {
        
            if (parse_while_statement (names, method)) {
                return 1;
            }
            
            continue;
        
        } else if (tok.kind == TOK_DO) {
        
            if (parse_do_statement (names, method)) {
                return 1;
            }
            
            continue;
        
        } else if (tok.kind == TOK_SWITCH) {
        
            if (parse_switch_statement (names, method)) {
                return 1;
            }
            
            continue;
        
        } else if (tok.kind == TOK_TRY) {
        
            if (parse_try_statement (names, method)) {
                return 1;
            }
            
            continue;
        
        } else if (tok.kind == TOK_CONTINUE) {
        
            if (parse_continue_statement (method)) {
            
                failed = 1;
                goto recover_statement;
            
            }
        
        } else if (tok.kind == TOK_RETURN) {
        
            if (parse_return_statement (names, method)) {
            
                failed = 1;
                goto recover_statement;
            
            }
        
        } else if (tok.kind == TOK_THROW) {
        
            if (parse_throw_statement (names, method)) {
            
                failed = 1;
                goto recover_statement;
            
            }
        
        } else if (tok.kind == TOK_GOTO) {
        
            if (parse_goto_statement (method)) {
            
                failed = 1;
                goto recover_statement;
            
            }
        
        } else if (tok.kind == TOK_BYTE || tok.kind == TOK_CHAR
            || tok.kind == TOK_SHORT || tok.kind == TOK_INT
                || tok.kind == TOK_LONG || tok.kind == TOK_FLOAT
                    || tok.kind == TOK_DOUBLE || tok.kind == TOK_BOOLEAN
                        || token_starts_reference_local_declaration ()) {
        
            if (parse_local_declaration (names, method)) {
            
                failed = 1;
                goto recover_statement;
            
            }
        
        } else {
        
            if (parse_statement (names, method)) {
            
                failed = 1;
                goto recover_statement;
            
            }
        
        }
        
        get_token ();
        continue;
        
recover_statement:
        
        /* A failed simple statement is discarded through its terminator. */
        while (tok.kind != TOK_EOF) {
        
            if (tok.kind == TOK_RBRACE && get_token_brace_depth () < depth) {
                break;
            }
            
            if (tok.kind == TOK_SEMI && get_token_brace_depth () == depth) {
                get_token ();
                break;
            }
            
            get_token ();
        
        }
        
        if (tok.kind == TOK_EOF) {
            return 1;
        }
    
    }
    
    return failed;

}

static int parse_block_statements (struct source_names *names, struct method_info *method) {

    int ret;
    
    method->local_scope_depth++;
    ret = parse_block_statements_inner (names, method);
    
    leave_local_scope (method);
    return ret;

}

static const char *field_array_next (const char *p, char **value_out);
static int field_initializer_is_array (const char *type_desc, const char *text);
static unsigned long field_array_count (const char *text);

static int add_static_array_initializers (struct source_names *names, struct method_info *method) {

    unsigned long i;
    
    if (!(method->flags & METHOD_STATIC) || strcmp (method->java_name, "main") != 0) {
        return 0;
    }
    
    for (i = 0; i < names->nb_fields; i++) {
    
        unsigned long array_local;
        unsigned long size_local;
        unsigned long index_local;
        unsigned long value_local;
        unsigned long count;
        unsigned long j;
        
        char temp_name[64];
        char literal[64];
        char source[64];
        
        const char *p;
        
        if (!(names->fields[i].flags & METHOD_STATIC) || !field_initializer_is_array (names->fields[i].type, names->fields[i].init_text)) {
            continue;
        }
        
        count = field_array_count (names->fields[i].init_text);
        p = strchr (names->fields[i].init_text, '|');
        
        sprintf (temp_name, "__j2d_static_array_size_%lu", method->nb_locals);
        
        if (add_local_info (method, temp_name, "I", &size_local)) {
            return 1;
        }
        
        sprintf (literal, "%lu", count);
        add_local_statement (method, size_local, ARG_INT, 0, literal);
        
        sprintf (temp_name, "__j2d_static_array_%lu", method->nb_locals);
        
        if (add_local_info (method, temp_name, names->fields[i].type, &array_local)) {
            return 1;
        }
        
        sprintf (source, "$%lu", size_local);
        add_local_statement (method, array_local, ARG_NEW_ARRAY, source, names->fields[i].type);
        
        sprintf (temp_name, "__j2d_static_array_index_%lu", method->nb_locals);
        
        if (add_local_info (method, temp_name, "I", &index_local)) {
            return 1;
        }
        
        sprintf (temp_name, "__j2d_static_array_value_%lu", method->nb_locals);
        
        if (add_local_info (method, temp_name, names->fields[i].type + 1, &value_local)) {
            return 1;
        }
        
        for (j = 0; j < count; j++) {
        
            char *value = 0;
            
            if (!(p = field_array_next (p, &value))) {
                return 1;
            }
            
            sprintf (temp_name, "__j2d_static_array_index_%lu", method->nb_locals);
            sprintf (literal, "%lu", j);
            
            add_local_statement (method, index_local, ARG_INT, 0, literal);
            add_local_statement (method, value_local, names->fields[i].type[1] == 'L' || names->fields[i].type[1] == '[' ? ARG_STRING : ARG_INT, 0, value);
            
            add_array_put_statement (method, array_local, index_local, value_local);
            free (value);
        
        }
        
        sprintf (source, "$%lu", array_local);
        add_field_statement (method, names->fields[i].name, ARG_ARITHMETIC, source, "");
    
    }
    
    return 0;

}

static int parse_method_body (struct source_names *names, struct method_info *method) {

    int ret;
    
    if (collect_declarations_only || validate_declarations_only) {
    
        unsigned long depth = 1;
        
        while (depth) {
        
            get_token ();
            
            if (tok.kind == TOK_EOF) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "unexpected end of file in method body");
                return 1;
            
            }
            
            if (tok.kind == TOK_LBRACE) {
                depth++;
            } else if (tok.kind == TOK_RBRACE) {
                depth--;
            }
        
        }
        
        return 0;
    
    }
    
    get_token ();
    
    if (add_static_array_initializers (names, method)) {
        return 1;
    }
    
    if (!(ret = parse_block_statements (names, method))) {
        ret = resolve_named_gotos (method);
    }
    
    return ret;

}

static double field_initializer_numeric_value (const char *text) {

    char *tmp;
    
    double value;
    unsigned long len;
    
    len = strlen (text);
    tmp = xstrdup (text);
    
    if (len && (tmp[len - 1] == 'f' || tmp[len - 1] == 'F' || tmp[len - 1] == 'd' || tmp[len - 1] == 'D' || tmp[len - 1] == 'l' || tmp[len - 1] == 'L')) {
        tmp[len - 1] = '\0';
    }
    
    value = strtod (tmp, 0);
    
    free (tmp);
    return value;

}

static void free_field_mul_expr (struct field_mul_expr *expr) {

    free (expr->field);
    free (expr->factors);
    free (expr->literal_text);
    
    memset (expr, 0, sizeof (*expr));

}

static void append_field_mul_operation (struct field_mul_expr *expr, int operation, long value) {

    char number[64];

    if (expr->factors_len) {
        append_text (&expr->factors, &expr->factors_len, "*");
    }
    
    if (operation == TOK_PLUS) {
        append_text (&expr->factors, &expr->factors_len, "+");
    } else if (operation == TOK_MINUS) {
        append_text (&expr->factors, &expr->factors_len, "-");
    } else if (operation == TOK_BSLASH) {
        append_text (&expr->factors, &expr->factors_len, "/");
    } else if (operation == TOK_MOD) {
        append_text (&expr->factors, &expr->factors_len, "%");
    } else if (operation == TOK_LSH) {
        append_text (&expr->factors, &expr->factors_len, "<");
    } else if (operation == TOK_RSH) {
        append_text (&expr->factors, &expr->factors_len, ">");
    } else if (operation == TOK_URSH) {
        append_text (&expr->factors, &expr->factors_len, "u");
    } else if (operation == TOK_AMPER) {
        append_text (&expr->factors, &expr->factors_len, "&");
    } else if (operation == TOK_CARET) {
        append_text (&expr->factors, &expr->factors_len, "^");
    } else if (operation == TOK_PIPE) {
        append_text (&expr->factors, &expr->factors_len, "|");
    }
    
    sprintf (number, "%ld", value);
    append_text (&expr->factors, &expr->factors_len, number);

}

static int operation_is_commutative (int operation) {
    return operation == TOK_PLUS || operation == TOK_STAR || operation == TOK_AMPER || operation == TOK_CARET || operation == TOK_PIPE;
}

static void append_field_runtime_operation (struct field_mul_expr *expr, int operation, const char *source) {

    if (expr->factors_len) {
        append_text (&expr->factors, &expr->factors_len, "*");
    }
    
    if (operation == TOK_PLUS) {
        append_text (&expr->factors, &expr->factors_len, "+");
    } else if (operation == TOK_MINUS) {
        append_text (&expr->factors, &expr->factors_len, "-");
    } else if (operation == TOK_BSLASH) {
        append_text (&expr->factors, &expr->factors_len, "/");
    } else if (operation == TOK_MOD) {
        append_text (&expr->factors, &expr->factors_len, "%");
    } else if (operation == TOK_LSH) {
        append_text (&expr->factors, &expr->factors_len, "<");
    } else if (operation == TOK_RSH) {
        append_text (&expr->factors, &expr->factors_len, ">");
    } else if (operation == TOK_URSH) {
        append_text (&expr->factors, &expr->factors_len, "u");
    } else if (operation == TOK_AMPER) {
        append_text (&expr->factors, &expr->factors_len, "&");
    } else if (operation == TOK_CARET) {
        append_text (&expr->factors, &expr->factors_len, "^");
    } else if (operation == TOK_PIPE) {
        append_text (&expr->factors, &expr->factors_len, "|");
    } else if (operation == TOK_EQEQ) {
        append_text (&expr->factors, &expr->factors_len, "=");
    } else if (operation == TOK_NOTEQ) {
        append_text (&expr->factors, &expr->factors_len, "q");
    } else if (operation == TOK_LESS) {
        append_text (&expr->factors, &expr->factors_len, "l");
    } else if (operation == TOK_LTEQ) {
        append_text (&expr->factors, &expr->factors_len, "e");
    } else if (operation == TOK_GREATER) {
        append_text (&expr->factors, &expr->factors_len, "g");
    } else if (operation == TOK_GTEQ) {
        append_text (&expr->factors, &expr->factors_len, "h");
    } else if (operation == TOK_LOGAND) {
        append_text (&expr->factors, &expr->factors_len, "a");
    } else if (operation == TOK_LOGOR) {
        append_text (&expr->factors, &expr->factors_len, "o");
    }
    
    append_text (&expr->factors, &expr->factors_len, source);

}

static int parse_field_expression (struct source_names *names, struct field_mul_expr *expr);

static int parse_field_primary (struct source_names *names, struct field_mul_expr *expr) {

    memset (expr, 0, sizeof (*expr));
    
    if (tok.kind == TOK_LPAREN) {
    
        get_token ();
        
        if (tok.kind == TOK_BYTE || tok.kind == TOK_CHAR || tok.kind == TOK_SHORT
            || tok.kind == TOK_INT || tok.kind == TOK_LONG || tok.kind == TOK_FLOAT
                || tok.kind == TOK_DOUBLE) {
        
            char *cast_desc = 0;
            
            if (parse_type_name (names, 0, &cast_desc)) {
                return 1;
            }
            
            if (expect_token (TOK_RPAREN, "')' after cast type")) {
            
                free (cast_desc);
                return 1;
            
            }
            
            get_token ();
            
            if (parse_field_primary (names, expr)) {
            
                free (cast_desc);
                return 1;
            
            }
            
            if (expr->has_field) {
            
                if (expr->factors_len) {
                    append_text (&expr->factors, &expr->factors_len, "*");
                }
                
                append_text (&expr->factors, &expr->factors_len, "!");
                append_text (&expr->factors, &expr->factors_len, cast_desc);
                
                expr->type = cast_desc[0];
            
            } else if (expr->is_constant) {
                expr->type = cast_desc[0];
            }
            
            free (cast_desc);
            return 0;
        
        }
        
        if (parse_field_expression (names, expr)) {
            return 1;
        }
        
        if (expect_token (TOK_RPAREN, "')' after parenthesized field initializer")) {
        
            free_field_mul_expr (expr);
            return 1;
        
        }
        
        get_token ();
        return 0;
    
    }
    
    if (tok.kind == TOK_IDENT) {
    
        struct field_info *field = find_field_info_by_name (names, tok.ident);
        
        char *target = xstrdup (tok.ident);
        unsigned long target_len = strlen (target);
        
        get_token ();
        
        if (tok.kind == TOK_DOT) {
        
            char *method = 0;
            
            for (;;) {
            
                get_token ();
                
                if (expect_ident ("method name in field initializer")) {
                
                    free (target);
                    return 1;
                
                }
                
                method = xstrdup (tok.ident);
                get_token ();
                
                if (tok.kind == TOK_LPAREN) {
                    break;
                }
                
                if (tok.kind != TOK_DOT) {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                        tok.start, tok.caret, "expected '.' or '(' in field initializer method call");
                    
                    free (target);
                    free (method);
                    
                    return 1;
                
                }
                
                append_text (&target, &target_len, ".");
                append_text (&target, &target_len, method);
                
                free (method);
                method = 0;
            
            }
            
            get_token ();
            
            if (expect_token (TOK_RPAREN, "')' in field initializer method call")) {
            
                free (target);
                free (method);
                
                return 1;
            
            }
            
            get_token ();
            
            expr->field = make_field_call_initializer_text (target, method);
            expr->type = 'D';
            expr->has_field = 1;
            
            free (target);
            free (method);
            
            return 0;
        
        }
        
        if (!field) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "unknown field '%s' in field initializer", target);
            
            free (target);
            return 1;
        
        }
        
        expr->type = field->type[0];
        expr->has_field = 1;
        expr->field = target;
        
        return 0;
    
    }
    
    if (tok.kind == TOK_PPSTR) {
    
        expr->literal_text = xstrdup (tok.ident);
        expr->type = 'L';
        expr->is_constant = 1;
        
        get_token ();
        return 0;
    
    }
    
    if (tok.kind == TOK_TRUE || tok.kind == TOK_FALSE) {
    
        expr->constant = tok.kind == TOK_TRUE;
        expr->type = 'Z';
        expr->is_constant = 1;
        
        get_token ();
        return 0;
    
    }
    
    if (tok.kind == TOK_CINT || tok.kind == TOK_CLONG || tok.kind == TOK_CFLOAT || tok.kind == TOK_CDOUBLE) {
    
        if (tok.kind == TOK_CFLOAT || tok.kind == TOK_CDOUBLE) {
        
            expr->literal_text = xstrdup (tok.ident);
            expr->type = tok.kind == TOK_CFLOAT ? 'F' : 'D';
        
        } else if (tok.kind == TOK_CLONG) {
        
            expr->literal_text = xstrdup (tok.ident);
            expr->constant = field_initializer_to_long (tok.ident);
            expr->type = 'J';
        
        } else {
        
            expr->constant = field_initializer_to_long (tok.ident);
            expr->type = 'I';
        
        }
        
        expr->is_constant = 1;
        get_token ();
        
        return 0;
    
    }
    
    report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
        tok.start, tok.caret, "expected field, numeric literal or parenthesized expression");
    
    return 1;

}

static void append_field_constant_operation (struct field_mul_expr *expr, int operation, const struct field_mul_expr *constant_expr, int reverse) {

    const char *text = constant_expr->literal_text;
    char value[64];
    
    if (!text) {
    
        sprintf (value, "%ld", constant_expr->constant);
        text = value;
    
    }
    
    if (expr->factors_len) {
        append_text (&expr->factors, &expr->factors_len, "*");
    }
    
    if (reverse) {
        append_text (&expr->factors, &expr->factors_len, "~");
    }
    
    if (operation == TOK_PLUS) {
        append_text (&expr->factors, &expr->factors_len, "+");
    } else if (operation == TOK_MINUS) {
        append_text (&expr->factors, &expr->factors_len, "-");
    } else if (operation == TOK_BSLASH) {
        append_text (&expr->factors, &expr->factors_len, "/");
    } else if (operation == TOK_MOD) {
        append_text (&expr->factors, &expr->factors_len, "%");
    } else if (operation == TOK_LSH) {
        append_text (&expr->factors, &expr->factors_len, "<");
    } else if (operation == TOK_RSH) {
        append_text (&expr->factors, &expr->factors_len, ">");
    } else if (operation == TOK_URSH) {
        append_text (&expr->factors, &expr->factors_len, "u");
    } else if (operation == TOK_AMPER) {
        append_text (&expr->factors, &expr->factors_len, "&");
    } else if (operation == TOK_CARET) {
        append_text (&expr->factors, &expr->factors_len, "^");
    } else if (operation == TOK_PIPE) {
        append_text (&expr->factors, &expr->factors_len, "|");
    } else if (operation == TOK_EQEQ) {
        append_text (&expr->factors, &expr->factors_len, "=");
    } else if (operation == TOK_NOTEQ) {
        append_text (&expr->factors, &expr->factors_len, "q");
    } else if (operation == TOK_LESS) {
        append_text (&expr->factors, &expr->factors_len, "l");
    } else if (operation == TOK_LTEQ) {
        append_text (&expr->factors, &expr->factors_len, "e");
    } else if (operation == TOK_GREATER) {
        append_text (&expr->factors, &expr->factors_len, "g");
    } else if (operation == TOK_GTEQ) {
        append_text (&expr->factors, &expr->factors_len, "h");
    } else if (operation == TOK_LOGAND) {
        append_text (&expr->factors, &expr->factors_len, "a");
    } else if (operation == TOK_LOGOR) {
        append_text (&expr->factors, &expr->factors_len, "o");
    }
    
    append_text (&expr->factors, &expr->factors_len, text);

}

static char promoted_numeric_type (char left, char right, int operation) {

    if (operation == TOK_LSH || operation == TOK_RSH || operation == TOK_URSH) {
        return left;
    }
    
    if (left == 'D' || right == 'D') {
        return 'D';
    }
    
    if (left == 'F' || right == 'F') {
        return 'F';
    }
    
    if (left == 'J' || right == 'J') {
        return 'J';
    }
    
    return 'I';

}

static char arithmetic_literal_type (const char *text) {

    unsigned long len = strlen (text);
    const char *p;
    
    if (len && (text[len - 1] == 'f' || text[len - 1] == 'F')) {
        return 'F';
    }
    
    if (len && (text[len - 1] == 'd' || text[len - 1] == 'D')) {
        return 'D';
    }
    
    if (len && (text[len - 1] == 'l' || text[len - 1] == 'L')) {
        return 'J';
    }
    
    for (p = text; *p; p++) {
    
        if (*p == '.' || *p == 'e' || *p == 'E') {
            return 'D';
        }
    
    }
    
    return 'I';

}

static double floating_remainder (double left, double right) {

    long quotient;
    double divided;
    
    if (right == 0.0) {
        return 0.0 / 0.0;
    }
    
    divided = left / right;
    quotient = (long) divided;
    
    return left - ((double) quotient * right);

}

static void set_folded_floating_constant (struct field_mul_expr *expr, double value, char type) {

    char text[96];
    
    if (type == 'F') {
    
        float rounded = (float) value;
        sprintf (text, "%.9gf", (double) rounded);
    
    } else {
        sprintf (text, "%.17g", value);
    }
    
    free (expr->literal_text);
    expr->literal_text = xstrdup (text);
    
    expr->constant = 0;
    expr->type = type;
    expr->is_constant = 1;

}

static void field_integer_constant_value (const struct field_mul_expr *expr, int64_s *value) {

    if (expr->literal_text) {
        parse_integer_text64 (value, expr->literal_text);
    } else {
    
        value->low = ((unsigned long) expr->constant) & U32_MASK;
        value->high = (value->low & 0x80000000UL) ? U32_MASK : 0;
    
    }

}

static int fold_field_integer_constants (struct field_mul_expr *left, const struct field_mul_expr *right, int operation) {

    int64_s lvalue, rvalue;
    char result_type = promoted_numeric_type (left->type, right->type, operation);
    
    char text[32];
    unsigned long shift;
    
    int negative;
    int left_negative, right_negative;
    int result;
    
    field_integer_constant_value (left, &lvalue);
    field_integer_constant_value (right, &rvalue);
    
    if (operation == TOK_EQEQ || operation == TOK_NOTEQ || operation == TOK_LESS
        || operation == TOK_LTEQ || operation == TOK_GREATER || operation == TOK_GTEQ
            || operation == TOK_LOGAND || operation == TOK_LOGOR) {
    
        if (operation == TOK_EQEQ) {
            result = eq64 (lvalue, rvalue);
        } else if (operation == TOK_NOTEQ) {
            result = neq64 (lvalue, rvalue);
        } else if (operation == TOK_LESS) {
            result = lt64_signed (lvalue, rvalue);
        } else if (operation == TOK_LTEQ) {
            result = lte64_signed (lvalue, rvalue);
        } else if (operation == TOK_GREATER) {
            result = gt64_signed (lvalue, rvalue);
        } else if (operation == TOK_GTEQ) {
            result = gte64_signed (lvalue, rvalue);
        } else if (operation == TOK_LOGAND) {
            result = !is_zero64 (lvalue) && !is_zero64 (rvalue);
        } else {
            result = !is_zero64 (lvalue) || !is_zero64 (rvalue);
        }
        
        free (left->literal_text);
        
        left->literal_text = 0;
        left->constant = result;
        left->type = 'Z';
        
        return 0;
    
    }
    
    if (operation == TOK_BSLASH || operation == TOK_MOD) {
    
        if (is_zero64 (rvalue)) {
            return 2;
        }
        
        left_negative = (lvalue.high & 0x80000000UL) != 0;
        right_negative = (rvalue.high & 0x80000000UL) != 0;
        
        if (left_negative) {
            neg64 (&lvalue);
        }
        
        if (right_negative) {
            neg64 (&rvalue);
        }
        
        if (operation == TOK_BSLASH) {
        
            div64 (&lvalue, rvalue);
            negative = left_negative != right_negative;
        
        } else {
        
            mod64 (&lvalue, rvalue);
            negative = left_negative;
        
        }
        
        if (negative) {
            neg64 (&lvalue);
        }
    
    } else if (operation == TOK_STAR) {
        mul64 (&lvalue, rvalue);
    } else if (operation == TOK_PLUS) {
        add64 (&lvalue, rvalue);
    } else if (operation == TOK_MINUS) {
        sub64 (&lvalue, rvalue);
    } else if (operation == TOK_AMPER) {
        and64 (&lvalue, rvalue);
    } else if (operation == TOK_CARET) {
        xor64 (&lvalue, rvalue);
    } else if (operation == TOK_PIPE) {
        or64 (&lvalue, rvalue);
    } else if (operation == TOK_LSH || operation == TOK_RSH || operation == TOK_URSH) {
    
        shift = rvalue.low & (left->type == 'J' ? 63UL : 31UL);
        negative = left->type == 'J' ? (lvalue.high & 0x80000000UL) != 0 : (lvalue.low & 0x80000000UL) != 0;
        
        if (left->type != 'J') {
        
            if (operation == TOK_LSH) {
                lvalue.low = (lvalue.low << shift) & U32_MASK;
            } else if (shift) {
            
                lvalue.low = (lvalue.low & U32_MASK) >> shift;
                
                if (operation == TOK_RSH && negative) {
                    lvalue.low |= (U32_MASK << (32 - shift)) & U32_MASK;
                }
            
            }
            
            lvalue.high = (lvalue.low & 0x80000000UL) ? U32_MASK : 0;
        
        } else if (operation == TOK_LSH) {
            shl64 (&lvalue, (int) shift);
        } else if (shift) {
        
            shr64 (&lvalue, (int) shift);
            
            if (operation == TOK_RSH && negative) {
            
                int64_s fill = { U32_MASK, U32_MASK };
                
                shl64 (&fill, (int) (64 - shift));
                or64 (&lvalue, fill);
            
            }
        
        }
    
    } else {
        return 1;
    }
    
    free (left->literal_text);
    
    left->literal_text = 0;
    left->type = result_type;
    
    if (result_type == 'J') {
    
        sprintf (text, "0x%08lX%08lXL", lvalue.high & U32_MASK, lvalue.low & U32_MASK);
        
        left->literal_text = xstrdup (text);
        left->constant = (long) lvalue.low;
    
    } else {
    
        lvalue.low &= U32_MASK;
        left->constant = (long) (lvalue.low & 0x7FFFFFFFUL);
        
        if (lvalue.low & 0x80000000UL) {
            left->constant += (-2147483647L - 1L);
        }
    
    }
    
    return 0;

}

static int merge_field_expression (struct field_mul_expr *left, struct field_mul_expr *right, int operation) {

    struct field_mul_expr *field_expr;
    struct field_mul_expr *constant_expr;
    
    if (left->type == 'N' || right->type == 'N') {
    
        if (operation != TOK_EQEQ && operation != TOK_NOTEQ) {
            return 1;
        }
        
        if (left->type == 'N' && right->type == 'N') {
        
            left->constant = operation == TOK_EQEQ;
            left->type = 'Z';
            
            return 0;
        
        }
        
        if (left->type == 'N') {
        
            if (!right->has_field || (right->type != 'L' && right->type != '[')) {
                return 1;
            }
            
            field_expr = right;
            constant_expr = left;
        
        } else {
        
            if (!left->has_field || (left->type != 'L' && left->type != '[')) {
                return 1;
            }
            
            field_expr = left;
            constant_expr = right;
        
        }
        
        append_field_constant_operation (field_expr, operation, constant_expr, 0);
        
        if (field_expr != left) {
        
            *left = *field_expr;
            memset (field_expr, 0, sizeof (*field_expr));
        
        }
        
        left->type = 'Z';
        return 0;
    
    }
    
    if (left->has_field && right->has_field) {
    
        append_field_runtime_operation (left, operation, right->field);
        
        if (right->factors_len) {
        
            append_text (&left->factors, &left->factors_len, "*");
            append_text (&left->factors, &left->factors_len, right->factors);
        
        }
        
        left->type = (operation == TOK_EQEQ || operation == TOK_NOTEQ || operation == TOK_LESS || operation == TOK_LTEQ || operation == TOK_GREATER || operation == TOK_GTEQ || operation == TOK_LOGAND || operation == TOK_LOGOR) ? 'Z' : promoted_numeric_type (left->type, right->type, operation);
        return 0;
    
    }
    
    if (left->is_constant && right->is_constant) {
    
        if (left->type != 'F' && left->type != 'D' && right->type != 'F' && right->type != 'D') {
            return fold_field_integer_constants (left, right, operation);
        }
        
        if (operation == TOK_EQEQ || operation == TOK_NOTEQ || operation == TOK_LESS
            || operation == TOK_LTEQ || operation == TOK_GREATER || operation == TOK_GTEQ
                || operation == TOK_LOGAND || operation == TOK_LOGOR) {
        
            double lvalue = left->literal_text ? field_initializer_numeric_value (left->literal_text) : (double) left->constant;
            double rvalue = right->literal_text ? field_initializer_numeric_value (right->literal_text) : (double) right->constant;
            int result;
            
            if (operation == TOK_EQEQ) {
                result = lvalue == rvalue;
            } else if (operation == TOK_NOTEQ) {
                result = lvalue != rvalue;
            } else if (operation == TOK_LESS) {
                result = lvalue < rvalue;
            } else if (operation == TOK_LTEQ) {
                result = lvalue <= rvalue;
            } else if (operation == TOK_GREATER) {
                result = lvalue > rvalue;
            } else if (operation == TOK_GTEQ) {
                result = lvalue >= rvalue;
            } else if (operation == TOK_LOGAND) {
                result = (lvalue != 0.0) && (rvalue != 0.0);
            } else {
                result = (lvalue != 0.0) || (rvalue != 0.0);
            }
            
            free (left->literal_text);
            
            left->literal_text = 0;
            left->constant = result;
            left->type = 'Z';
            left->is_constant = 1;
            
            return 0;
        
        }
        
        if (left->literal_text || right->literal_text) {
        
            double lvalue = left->literal_text ? field_initializer_numeric_value (left->literal_text) : (double) left->constant;
            double rvalue = right->literal_text ? field_initializer_numeric_value (right->literal_text) : (double) right->constant;
            
            char result_type = promoted_numeric_type (left->type, right->type, operation);
            double result;
            
            if (operation == TOK_AMPER || operation == TOK_CARET || operation == TOK_PIPE || operation == TOK_LSH || operation == TOK_RSH || operation == TOK_URSH) {
                return 1;
            }
            
            if (operation == TOK_STAR) {
                result = lvalue * rvalue;
            } else if (operation == TOK_BSLASH) {
                result = lvalue / rvalue;
            } else if (operation == TOK_MOD) {
            
                if (rvalue == 0.0) {
                    return 2;
                }
                
                result = floating_remainder (lvalue, rvalue);
            
            } else if (operation == TOK_MINUS) {
                result = lvalue - rvalue;
            } else {
                result = lvalue + rvalue;
            }
            
            set_folded_floating_constant (left, result, result_type);
            return 0;
        
        }
    
    }
    
    if (left->has_field) {
    
        field_expr = left;
        constant_expr = right;
    
    } else {
    
        field_expr = right;
        constant_expr = left;
    
    }
    
    if (!field_expr->has_field || !constant_expr->is_constant) {
        return 1;
    }
    
    if ((field_expr->type == 'F' || field_expr->type == 'D'
        || constant_expr->type == 'F' || constant_expr->type == 'D')
            && (operation == TOK_MOD || operation == TOK_LSH || operation == TOK_RSH
                || operation == TOK_URSH || operation == TOK_AMPER
                    || operation == TOK_CARET || operation == TOK_PIPE)) {
        return 1;
    }
    
    append_field_constant_operation (field_expr, operation, constant_expr,
        field_expr != left && !operation_is_commutative (operation));
    
    if (field_expr != left) {
    
        free (left->factors);
        free (left->field);
        
        *left = *field_expr;
        memset (field_expr, 0, sizeof (*field_expr));
    
    }
    
    left->type = (operation == TOK_EQEQ || operation == TOK_NOTEQ || operation == TOK_LESS || operation == TOK_LTEQ || operation == TOK_GREATER || operation == TOK_GTEQ || operation == TOK_LOGAND || operation == TOK_LOGOR) ? 'Z' : promoted_numeric_type (left->type, constant_expr->type, operation);
    return 0;

}

static int parse_field_binary_level (struct source_names *names,

    struct field_mul_expr *expr, int level) {

    if (level == 0) {
    
        if (parse_field_primary (names, expr)) {
            return 1;
        }
    
    } else if (parse_field_binary_level (names, expr, level - 1)) {
        return 1;
    }
    
    while (binary_operator_at_level (tok.kind, level)) {
    
        struct field_mul_expr right;
        int operation = tok.kind;
        int merge_result;
        
        get_token ();
        
        if (level == 0) {
        
            if (parse_field_primary (names, &right)) {
            
                free_field_mul_expr (expr);
                return 1;
            
            }
        
        } else if (parse_field_binary_level (names, &right, level - 1)) {
        
            free_field_mul_expr (expr);
            return 1;
        
        }
        
        merge_result = merge_field_expression (expr, &right, operation);
        
        if (merge_result) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, merge_result == 2
                    ? "division or remainder by zero in field initializer"
                    : "operation on these field values is not supported yet");
            
            free_field_mul_expr (&right);
            free_field_mul_expr (expr);
            
            return 1;
        
        }
        
        free_field_mul_expr (&right);
    
    }
    
    return 0;

}

static int parse_field_expression (struct source_names *names, struct field_mul_expr *expr) {

    struct field_mul_expr when_true;
    struct field_mul_expr when_false;
    
    int result;
    
    if (parse_field_binary_level (names, expr, 9)) {
        return 1;
    }
    
    if (tok.kind != TOK_QMARK) {
        return 0;
    }
    
    get_token ();
    
    if (parse_field_expression (names, &when_true)) {
    
        free_field_mul_expr (expr);
        return 1;
    
    }
    
    if (expect_token (TOK_COLON, "':' in conditional expression")) {
    
        free_field_mul_expr (&when_true);
        free_field_mul_expr (expr);
        
        return 1;
    
    }
    
    get_token ();
    
    if (parse_field_expression (names, &when_false)) {
    
        free_field_mul_expr (&when_true);
        free_field_mul_expr (expr);
        
        return 1;
    
    }
    
    result = fold_conditional_expression (expr, &when_true, &when_false, expr);
    
    if (result) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, result == 1
                ? "conditional expression requires a boolean condition and compatible numeric result values"
                    : "division or remainder by zero in conditional expression");
        
        free_field_mul_expr (&when_false);
        free_field_mul_expr (&when_true);
        free_field_mul_expr (expr);
        
        return 1;
    
    }
    
    free_field_mul_expr (&when_false);
    free_field_mul_expr (&when_true);
    
    return 0;

}

static int field_initializer_is_array (const char *type_desc, const char *text) {
    return type_desc && type_desc[0] == '[' && text && text[0] == 'A';
}

static const char *field_array_next (const char *p, char **value_out) {

    unsigned long len = 0;
    char *value;
    
    if (!p || *p != '|') {
        return 0;
    }
    
    p++;
    
    while (*p >= '0' && *p <= '9') {
    
        len = len * 10 + (unsigned long) (*p - '0');
        p++;
    
    }
    
    if (*p != ':') {
        return 0;
    }
    
    p++;
    
    value = xmalloc (len + 1);
    memcpy (value, p, len);
    
    value[len] = '\0';
    *value_out = value;
    
    return p + len;

}

static unsigned long field_array_count (const char *text) {
    return text && text[0] == 'A' ? strtoul (text + 1, 0, 10) : 0;
}

static int array_initializer_element_compatible (const char *element_type, const struct field_mul_expr *expr) {

    if (!element_type || !expr || !expr->is_constant || expr->has_field) {
        return 0;
    }
    
    if (element_type[0] == 'L' || element_type[0] == '[') {
        return expr->type == 'L';
    }
    
    if (expr->type == 'L') {
        return 0;
    }
    
    if (element_type[0] == 'F' || element_type[0] == 'D') {
        return 1;
    }
    
    return expr->type != 'F' && expr->type != 'D';

}

static int parse_array_field_initializer (struct source_names *names, const char *type_desc, char **init_text_out) {

    char count_text[32];
    char *text;
    
    unsigned long text_len;
    unsigned long count = 0;
    
    const char *element_type;
    
    if (!type_desc || type_desc[0] != '[') {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "array initializer is not compatible with field type '%s'", type_desc);
        return 1;
    
    }
    
    element_type = type_desc + 1;
    
    text = xstrdup ("A0");
    text_len = 2;
    
    get_token ();
    
    while (tok.kind != TOK_RBRACE) {
    
        struct field_mul_expr expr;
        
        char value[128];
        char len_text[32];
        
        char *literal_text;
        const char *literal;
        
        unsigned long len;
        memset (&expr, 0, sizeof (expr));
        
        if (parse_field_expression (names, &expr)) {
        
            free (text);
            return 1;
        
        }
        
        if (!array_initializer_element_compatible (element_type, &expr)) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "array initializer element is not compatible with type '%s'", element_type);
            
            free_field_mul_expr (&expr);
            free (text);
            
            return 1;
        
        }
        
        literal = expr.literal_text;
        
        if (!literal) {
        
            sprintf (value, "%ld", expr.constant);
            literal = value;
        
        }
        
        len = strlen (literal);
        
        if (expr.type == 'L' && len >= 2 && literal[0] == '"' && literal[len - 1] == '"') {
        
            literal++;
            len -= 2;
        
        }
        
        sprintf (len_text, "|%lu:", len);
        
        append_text (&text, &text_len, len_text);
        literal_text = xmalloc (len + 1);
        
        memcpy (literal_text, literal, len);
        literal_text[len] = '\0';
        
        append_text (&text, &text_len, literal_text);
        
        free_field_mul_expr (&expr);
        free (literal_text);
        
        count++;
        
        if (tok.kind == TOK_COMMA) {
        
            get_token ();
            
            if (tok.kind == TOK_RBRACE) {
                break;
            }
        
        } else if (tok.kind != TOK_RBRACE) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "expected ',' or '}' in array initializer");
            
            free (text);
            return 1;
        
        }
    
    }
    
    get_token ();
    
    sprintf (count_text, "A%lu", count);
    memmove (text + strlen (count_text), text + 2, text_len - 1);
    
    memcpy (text, count_text, strlen (count_text));
    *init_text_out = text;
    
    return 0;

}

static int parse_field_initializer (struct source_names *names, const char *field_name, unsigned int field_flags, const char *type_desc, char **init_text_out, unsigned long *init_line_out, const char **init_start_out, const char **init_caret_out, int *is_constructor_initializer) {

    struct field_mul_expr expr;
    char *text;
    
    *is_constructor_initializer = 0;
    *init_text_out = 0;
    
    get_token ();
    
    if (init_line_out) {
        *init_line_out = get_line_number ();
    }
    
    if (init_start_out) {
        *init_start_out = tok.start;
    }
    
    if (init_caret_out) {
        *init_caret_out = tok.caret;
    }
    
    if ((field_flags & METHOD_STATIC) && type_desc[0] == 'L' && tok.kind == TOK_IDENT && find_method_info_by_java_name (names, tok.ident)) {
    
        struct method_info *callee = find_method_info_by_java_name (names, tok.ident);
        struct method_info *clinit = 0;
        
        char *method_name = xstrdup (tok.ident);
        char *encoded_args = 0;
        char *params;
        char local_name[64], source[64];
        
        unsigned long result_local, i;
        unsigned long call_line = get_line_number ();
        
        const char *call_start = xstrdup (tok.start);
        const char *call_caret = call_start + (tok.caret - tok.start);
        
        if (!(callee->flags & METHOD_STATIC) || strcmp (callee->return_type, type_desc) != 0) {
        
            report_line_at (get_filename (), call_line, REPORT_ERROR, tok.start, tok.caret, "incompatible static method in field initializer");
            
            free (method_name);
            free ((char *) call_start);
            
            return 1;
        
        }
        
        for (i = 0; i < names->nb_methods; i++) {
        
            if (strcmp (names->methods[i].java_name, "<clinit>") == 0) {
            
                clinit = &names->methods[i];
                break;
            
            }
        
        }
        
        if (!clinit) {
        
            add_method (names, "<clinit>", "V", "", 0, 0, METHOD_STATIC);
            clinit = &names->methods[names->nb_methods - 1];
        
        }
        
        get_token ();
        
        if (expect_token (TOK_LPAREN, "'(' after method name")) {
        
            free (method_name);
            free ((char *) call_start);
            
            return 1;
        
        }
        
        get_token ();
        
        if (parse_own_call_arguments (names, clinit, &encoded_args, method_name)) {
        
            free (method_name);
            free ((char *) call_start);
            
            return 1;
        
        }
        
        if (expect_token (TOK_RPAREN, "')' after method arguments")) {
        
            free (method_name);
            free (encoded_args);
            free ((char *) call_start);
            
            return 1;
        
        }
        
        params = strchr (encoded_args, '|');
        
        if (!params) {
        
            free (method_name);
            free (encoded_args);
            free ((char *) call_start);
            
            return 1;
        
        }
        
        for (i = 0; i < names->nb_methods; i++) {
        
            struct method_info *candidate = &names->methods[i];
            
            if (strcmp (candidate->java_name, method_name) == 0 && (size_t) (params - encoded_args) == strlen (candidate->params) && strncmp (candidate->params, encoded_args, (size_t) (params - encoded_args)) == 0) {
                break;
            }
        
        }
        
        if (i == names->nb_methods) {
        
            report_line_at (get_filename (), call_line, REPORT_ERROR, call_start, call_caret, "no matching static method overload for '%s'", method_name);
            
            free (method_name);
            free (encoded_args);
            free ((char *) call_start);
            
            return 1;
        
        }
        
        get_token ();
        
        if (expect_token (TOK_SEMI, "';' after field initializer")) {
        
            free (method_name);
            free (encoded_args);
            free ((char *) call_start);
            
            return 1;
        
        }
        
        sprintf (local_name, "__j2d_static_field_result_%lu", clinit->nb_locals);
        
        if (add_local_info (clinit, local_name, type_desc, &result_local)) {
        
            free (method_name);
            free (encoded_args);
            free ((char *) call_start);
            
            return 1;
        
        }
        
        add_call_statement (clinit, names->class_name, method_name, ARG_RESULT_MULTI, encoded_args, result_local, call_line, call_start, call_caret, 0, 0);
        
        sprintf (source, "$%lu", result_local);
        add_field_statement (clinit, field_name, ARG_ARITHMETIC, source, "");
        
        free (method_name);
        free (encoded_args);
        
        return 0;
    
    }
    
    if (tok.kind == TOK_NEW) {
    
        char *new_type;
        char *argument = 0;
        char *text;
        
        const char *encoding;
        get_token ();
        
        if (expect_ident ("class name after 'new'")) {
            return 1;
        }
        
        new_type = resolve_class_type (names, tok.ident);
        
        if (strcmp (type_desc, new_type) != 0) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "incompatible types in object field initializer");
            
            free (new_type);
            return 1;
        
        }
        
        free (new_type);
        get_token ();
        
        if (expect_token (TOK_LPAREN, "'(' after class name")) {
            return 1;
        }
        
        get_token ();
        
        if (tok.kind == TOK_PPSTR) {
        
            argument = copy_string_literal (tok.ident);
            get_token ();
        
        }
        
        if (tok.kind != TOK_RPAREN) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unsupported constructor initializer argument list");
            
            free (argument);
            return 1;
        
        }
        
        get_token ();
        
        if (expect_token (TOK_SEMI, "';' after field initializer")) {
        
            free (argument);
            return 1;
        
        }
        
        encoding = argument ? "@new:S" : "@new:0";
        
        text = xmalloc (strlen (encoding) + (argument ? strlen (argument) : 0) + 1);
        strcpy (text, encoding);
        
        if (argument) {
            strcat (text, argument);
        }
        
        *init_text_out = text;
        *is_constructor_initializer = 1;
        
        free (argument);
        return 0;
    
    }
    
    if (tok.kind == TOK_LBRACE) {
    
        if (parse_array_field_initializer (names, type_desc, init_text_out)) {
            return 1;
        }
        
        if (tok.kind != TOK_SEMI) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "expected ';' after field initializer expression");
            
            free (*init_text_out);
            *init_text_out = 0;
            
            return 1;
        
        }
        
        return 0;
    
    }
    
    if (parse_field_expression (names, &expr)) {
        return 1;
    }
    
    if (tok.kind != TOK_SEMI) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "expected ';' after field initializer expression");
        
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if (expr.type == 'L' && type_desc[0] != 'L' && type_desc[0] != '[') {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "incompatible types in field initializer");
        
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if (type_desc[0] == 'L' && expr.type != 'L') {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "incompatible types in field initializer");
        
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if ((type_desc[0] == 'Z') != (expr.type == 'Z')) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "incompatible types in field initializer");
        
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if ((type_desc[0] == 'I' || type_desc[0] == 'S' || type_desc[0] == 'J'
        || type_desc[0] == 'B' || type_desc[0] == 'C')
            && (expr.type == 'D' || expr.type == 'F')
                && (!expr.factors || !strchr (expr.factors, '!'))) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "incompatible types: possible lossy conversion in field initializer");
        
        free_field_mul_expr (&expr);
        return 1;
    
    }
    
    if (!expr.has_field) {
    
        const char *literal = expr.literal_text;
        char value[64];
        
        if (!literal) {
        
            sprintf (value, "%ld", expr.constant);
            literal = value;
        
        }
        
        *init_text_out = xstrdup (literal);
        free_field_mul_expr (&expr);
        
        return 0;
    
    }
    
    if (!expr.factors) {
        expr.factors = xstrdup ("1");
    }
    
    if (field_initializer_is_call (expr.field)) {
    
        char *target = 0;
        char *method = 0;
        
        if (split_field_call_initializer (expr.field, &target, &method)) {
        
            free_field_mul_expr (&expr);
            return 1;
        
        }
        
        text = make_field_mul_di_initializer_text (target, method, expr.factors);
        
        free (target);
        free (method);
    
    } else {
        text = make_field_mul_field_initializer_text (expr.field, expr.factors);
    }
    
    free_field_mul_expr (&expr);
    
    *init_text_out = text;
    return 0;

}

static int skip_field_initializer (void) {
 
    int depth = 0;
    get_token ();
    
    while (tok.kind != TOK_EOF) {
    
        if (!depth && tok.kind == TOK_SEMI) {
            return 0;
        }
        
        if (tok.kind == TOK_LPAREN || tok.kind == TOK_LBRACE || tok.kind == TOK_LBRACK) {
            depth++;
        } else if (tok.kind == TOK_RPAREN || tok.kind == TOK_RBRACE || tok.kind == TOK_RBRACK) {
        
            if (depth) {
                depth--;
            }
        
        }
        
        get_token ();
    
    }
    
    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unexpected end of file in field initializer");
    return 1;

}

static int add_modifier_flag (unsigned int *flags, unsigned int flag) {

    if (*flags & flag) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "duplicate modifier");
        
        return 1;
    
    }
    
    *flags |= flag;
    return 0;

}

static int add_access_modifier_flag (unsigned int *flags, unsigned int flag) {

    unsigned int access = *flags & (METHOD_PUBLIC | METHOD_PRIVATE | METHOD_PROTECTED);
    
    if (access) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, access & flag
                ? "duplicate modifier" : "conflicting access modifiers");
        
        return 1;
    
    }
    
    *flags |= flag;
    return 0;

}

static int parse_throws_clause (struct source_names *names, char ***types_out, unsigned long *nb_types_out) {

    char **types = 0;
    unsigned long nb_types = 0;
    
    *types_out = 0;
    *nb_types_out = 0;
    
    if (tok.kind != TOK_THROWS) {
        return 0;
    }
    
    get_token ();
    
    for (;;) {
    
        char *type;
        
        if (parse_type_name (names, 0, &type)) {
        
            unsigned long i;
            
            for (i = 0; i < nb_types; i++) {
                free (types[i]);
            }
            
            free (types);
            return 1;
        
        }
        
        if (type[0] != 'L' && type[0] != '[') {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "throws type must be an object reference");
            
            free (type);
            
            while (nb_types) {
                free (types[--nb_types]);
            }
            
            free (types);
            return 1;
        
        }
        
        types = xrealloc (types, (nb_types + 1) * sizeof (*types));
        types[nb_types++] = type;
        
        if (tok.kind != TOK_COMMA) {
            break;
        }
        
        get_token ();
    
    }
    
    *types_out = types;
    *nb_types_out = nb_types;
    
    return 0;

}

static int method_declares_throw (struct method_info *method, const char *type) {

    unsigned long i;
    
    for (i = 0; i < method->nb_throws_types; i++) {
    
        if (declared_reference_assignable (type, method->throws_types[i])) {
            return 1;
        }
    
    }
    
    return 0;

}

static int parse_class_body (struct source_names *names);
static int parse_implements_clause (struct source_names *names);

static void copy_source_context (struct source_names *dest, struct source_names *src) {

    unsigned long i;
    memset (dest, 0, sizeof (*dest));
    
    if (src->package_name) {
        dest->package_name = xstrdup (src->package_name);
    }
    
    for (i = 0; i < src->nb_imports; i++) {
    
        add_import (dest, src->imports[i].name, src->imports[i].wildcard,
            src->imports[i].implicit, src->imports[i].filename,
                src->imports[i].line, src->imports[i].source_line,
                    src->imports[i].caret_offset);
    
    }

}

static int parse_nested_class_decl (struct source_names *names, unsigned int flags) {

    struct source_names nested;
    
    char *simple;
    char *binary;
    
    unsigned long len;
    int ret;
    
    copy_source_context (&nested, names);
    nested.class_flags = flags;
    
    get_token ();
    
    if (expect_ident ("class name")) {
    
        free_source_names (&nested);
        return 1;
    
    }
    
    simple = xstrdup (tok.ident);
    nested.class_name = xstrdup (simple);
    
    get_token ();
    
    if (tok.kind == TOK_EXTENDS) {
    
        get_token ();
        
        if (parse_type_name (&nested, 0, &nested.super_class)) {
        
            free (simple);
            
            free_source_names (&nested);
            return 1;
        
        }
    
    }
    
    if (tok.kind == TOK_IMPLEMENTS) {
    
        if (parse_implements_clause (&nested)) {
        
            free (simple);
            
            free_source_names (&nested);
            return 1;
        
        }
    
    }
    
    ret = parse_class_body (&nested);
    
    if (!ret && !has_constructor (&nested)) {
        add_method (&nested, "<init>", "V", "", 0, 0, METHOD_PUBLIC);
    }
    
    if (ret) {
    
        free (simple);
        
        free_source_names (&nested);
        return 1;
    
    }
    
    len = strlen (names->class_name) + strlen (simple) + 1;
    
    binary = xmalloc (len + 1);
    sprintf (binary, "%s$%s", names->class_name, simple);
    
    free (nested.class_name);
    nested.class_name = binary;
    
    free (simple);
    
    names->nested_classes = xrealloc (names->nested_classes,
        (names->nb_nested_classes + 1) * sizeof (*names->nested_classes));
    names->nested_classes[names->nb_nested_classes++] = nested;
    
    return 0;

}

static int parse_member_decl (struct source_names *names) {

    char *init_text = 0;
    
    int is_constructor_initializer = 0;
    unsigned long init_line = 0;
    
    const char *init_start = 0;
    const char *init_caret = 0;
    
    char *type_desc;
    char *member_name;
    
    unsigned int flags = 0;
    int is_constructor = 0;
    
    for (;;) {
    
        if (skip_annotations ()) {
            return 1;
        }
        
        if (!is_member_modifier (tok.kind)) {
            break;
        }
        
        if (tok.kind == TOK_PUBLIC) {
        
            if (add_access_modifier_flag (&flags, METHOD_PUBLIC)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_PRIVATE) {
        
            if (add_access_modifier_flag (&flags, METHOD_PRIVATE)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_PROTECTED) {
        
            if (add_access_modifier_flag (&flags, METHOD_PROTECTED)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_STATIC) {
        
            if (add_modifier_flag (&flags, METHOD_STATIC)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_FINAL) {
        
            if (add_modifier_flag (&flags, METHOD_FINAL)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_VOLATILE) {
        
            if (add_modifier_flag (&flags, FIELD_VOLATILE)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_TRANSIENT) {
        
            if (add_modifier_flag (&flags, FIELD_TRANSIENT)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_SYNCHRONIZED) {
        
            if (add_modifier_flag (&flags, METHOD_SYNCHRONIZED)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_NATIVE) {
        
            if (add_modifier_flag (&flags, METHOD_NATIVE)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_ABSTRACT) {
        
            if (add_modifier_flag (&flags, METHOD_ABSTRACT)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_STRICTFP) {
        
            if (add_modifier_flag (&flags, METHOD_STRICTFP)) {
                return 1;
            }
        
        }
        
        get_token ();
    
    }
    
    if (tok.kind == TOK_LBRACE && flags == METHOD_STATIC) {
    
        struct method_info *clinit = 0;
        unsigned long i;
        
        for (i = 0; i < names->nb_methods; i++) {
        
            if (strcmp (names->methods[i].java_name, "<clinit>") == 0) {
            
                clinit = &names->methods[i];
                break;
            
            }
        
        }
        
        if (!clinit) {
        
            add_method (names, "<clinit>", "V", "", 0, 0, METHOD_STATIC);
            clinit = &names->methods[names->nb_methods - 1];
        
        }
        
        return parse_method_body (names, clinit);
    
    }
    
    if (tok.kind == TOK_CLASS) {
    
        unsigned int class_flags = 0;
        
        if (flags & METHOD_PUBLIC) {
            class_flags |= CLASS_PUBLIC;
        }
        
        if (flags & METHOD_FINAL) {
            class_flags |= CLASS_FINAL;
        }
        
        if (flags & METHOD_ABSTRACT) {
            class_flags |= CLASS_ABSTRACT;
        }
        
        if (flags & METHOD_STRICTFP) {
            class_flags |= CLASS_STRICTFP;
        }
        
        return parse_nested_class_decl (names, class_flags);
    
    }
    
    if (tok.kind == TOK_IDENT && strcmp (tok.ident, names->class_name) == 0) {
    
        member_name = xstrdup (tok.ident);
        get_token ();
        
        if (tok.kind == TOK_LPAREN) {
        
            type_desc = xstrdup ("V");
            is_constructor = 1;
        
        } else {
        
            unsigned long array_depth = 0;
            type_desc = resolve_class_type (names, names->class_name);
            
            if (append_array_suffix (&array_depth)) {
            
                free (type_desc);
                free (member_name);
                
                return 1;
            
            }
            
            type_desc = apply_array_suffix (type_desc, array_depth);
            free (member_name);
            
            if (expect_ident ("member name")) {
            
                free (type_desc);
                return 1;
            
            }
            
            member_name = xstrdup (tok.ident);
            get_token ();
        
        }
    
    } else {
    
        if (parse_type_name (names, 1, &type_desc)) {
            return 1;
        }
        
        if (expect_ident ("member name")) {
        
            free (type_desc);
            return 1;
        
        }
        
        member_name = xstrdup (tok.ident);
        get_token ();
    
    }
    
    if (tok.kind != TOK_LPAREN) {
    
        unsigned long array_depth = 0;
        
        if (append_array_suffix (&array_depth)) {
        
            free (member_name);
            free (type_desc);
            
            return 1;
        
        }
        
        type_desc = apply_array_suffix (type_desc, array_depth);
        
        if (type_desc[0] == 'V') {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "field cannot have void type");
            
            free (type_desc);
            free (member_name);
            
            return 1;
        
        }
        
        if (flags & (METHOD_SYNCHRONIZED | METHOD_NATIVE | METHOD_ABSTRACT | METHOD_STRICTFP)) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "invalid modifier for field");
            
            free (type_desc);
            free (member_name);
            
            return 1;
        
        }
        
        if ((flags & METHOD_FINAL) && (flags & FIELD_VOLATILE)) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "field cannot be both final and volatile");
            
            free (type_desc);
            free (member_name);
            
            return 1;
        
        }
        
        if (tok.kind == TOK_ASSIGN) {
        
            if (parse_field_initializer (names, member_name, flags, type_desc, &init_text, &init_line, &init_start, &init_caret, &is_constructor_initializer)) {
            
                free (type_desc);
                free (member_name);
                
                return 1;
            
            }
        
        }
        
        if ((flags & METHOD_STATIC) && is_constructor_initializer) {
        
            report_line_at (get_filename (), init_line, REPORT_ERROR, init_start, init_caret, "static object construction in field initializers is not supported yet");
            
            free (init_text);
            free (type_desc);
            free (member_name);
            
            return 1;
        
        }
        
        if (expect_token (TOK_SEMI, "';'")) {
        
            free (type_desc);
            free (member_name);
            
            return 1;
        
        }
        
        add_field (names, member_name, type_desc, init_text, init_line, init_start, init_caret, is_constructor_initializer, flags & (METHOD_PUBLIC | METHOD_PRIVATE | METHOD_PROTECTED | METHOD_STATIC | METHOD_FINAL | FIELD_VOLATILE | FIELD_TRANSIENT));
        
        free (init_text);
        free (type_desc);
        free (member_name);
        
        return 0;
    
    }
    
    if (is_constructor && (flags & ~(METHOD_PUBLIC | METHOD_PRIVATE | METHOD_PROTECTED))) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
            tok.start, tok.caret, "invalid modifier for constructor");
        
        free (type_desc);
        free (member_name);
        
        return 1;
    
    }
    
    if (flags & FIELD_VOLATILE) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "method cannot be volatile");
        
        free (type_desc);
        free (member_name);
        
        return 1;
    
    }
    
    if (flags & FIELD_TRANSIENT) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "method cannot be transient");
        
        free (type_desc);
        free (member_name);
        
        return 1;
    
    }
    
    {
    
        char *params;
        
        char **throws_types = 0;
        char **param_names = 0;
        
        unsigned long nb_throws_types = 0;
        unsigned long nb_param_names = 0;
        
        if (parse_param_list (names, &params, &param_names, &nb_param_names)) {
        
            free (member_name);
            free (type_desc);
            
            return 1;
        
        }
        
        get_token ();
        
        if (parse_throws_clause (names, &throws_types, &nb_throws_types)) {
        
            free (type_desc);
            
            free (params);
            free (member_name);
            
            free_param_names (param_names, nb_param_names);
            return 1;
        
        }
        
        if (!is_constructor && (flags & (METHOD_NATIVE | METHOD_ABSTRACT))) {
        
            if (expect_token (TOK_SEMI, "';'") ) {
            
                free (type_desc);
                free (params);
                free (member_name);
                
                free_param_names (param_names, nb_param_names);
                return 1;
            
            }
            
            add_method (names, is_constructor ? "<init>" : member_name, type_desc, params, param_names, nb_param_names, flags);
            
            names->methods[names->nb_methods - 1].throws_types = throws_types;
            names->methods[names->nb_methods - 1].nb_throws_types = nb_throws_types;
            
            free (type_desc);
            free (params);
            free (member_name);
            
            return 0;
        
        }
        
        if (expect_token (TOK_LBRACE, "'{'") ) {
        
            free (type_desc);
            free (params);
            free (member_name);
            
            free_param_names (param_names, nb_param_names);
            return 1;
        
        }
        
        add_method (names, is_constructor ? "<init>" : member_name, type_desc, params, param_names, nb_param_names, flags);
        
        names->methods[names->nb_methods - 1].throws_types = throws_types;
        names->methods[names->nb_methods - 1].nb_throws_types = nb_throws_types;
        
        free (type_desc);
        free (params);
        free (member_name);
        
        return parse_method_body (names, &names->methods[names->nb_methods - 1]);
    
    }

}

static int parse_class_body (struct source_names *names) {

    int failed = 0;
    int depth;
    
    if (expect_token (TOK_LBRACE, "'{' after class name")) {
        return 1;
    }
    
    get_token ();
    depth = get_token_brace_depth ();
    
    while (tok.kind != TOK_RBRACE) {
    
        if (tok.kind == TOK_EOF) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unexpected end of file in class body");
            return 1;
        
        }
        
        if (parse_member_decl (names)) {
        
            failed = 1;
            
            /* Discard the rest of a malformed member, not the whole class. */
            while (tok.kind != TOK_EOF) {
            
                int token_depth = get_token_brace_depth ();
                
                if (tok.kind == TOK_RBRACE && token_depth < depth) {
                    break;
                }
                
                if (token_depth == depth && (tok.kind == TOK_SEMI || tok.kind == TOK_RBRACE)) {
                
                    get_token ();
                    break;
                
                }
                
                get_token ();
            
            }
            
            continue;
        
        }
        
        get_token ();
    
    }
    
    return failed;

}

static int parse_qualified_name (char **name, int *wildcard, const char *what, const char **source_line, unsigned long *caret_offset) {

    unsigned long len = 0;
    
    int allow_wildcard = wildcard != 0;
    int need_ident = 1;
    
    *name = 0;
    
    if (wildcard) {
        *wildcard = 0;
    }
    
    for (;;) {
    
        get_token ();
        
        if (need_ident) {
        
            if (allow_wildcard && tok.kind == TOK_STAR) {
            
                if (!len || name[0][len - 1] != '.') {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected '.' before '*' in import name");
                    return 1;
                
                }
                
                (*name)[len - 1] = '\0';
                
                *wildcard = 1;
                get_token ();
                
                if (tok.kind != TOK_SEMI) {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected ';' after wildcard import");
                    return 1;
                
                }
                
                return 0;
            
            }
            
            if (expect_ident (what)) {
                return 1;
            }
            
            if (!len && source_line && caret_offset) {
            
                *source_line = tok.start;
                *caret_offset = (unsigned long) (tok.caret - tok.start);
            
            }
            
            append_text (name, &len, tok.ident);
            
            need_ident = 0;
            continue;
        
        }
        
        if (tok.kind == TOK_DOT) {
        
            append_text (name, &len, ".");
            
            need_ident = 1;
            continue;
        
        }
        
        if (tok.kind == TOK_SEMI) {
            return 0;
        }
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected '.' or ';' after identifier");
        return 1;
    
    }

}

static int parse_package_decl (struct source_names *names) {

    if (names->package_name) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "duplicate package declaration");
        return 1;
    
    }
    
    return parse_qualified_name (&names->package_name, 0, "identifier in package name", 0, 0);
}

static int parse_import_decl (struct source_names *names) {

    char *name = 0;
    int wildcard = 0;
    
    const char *filename = get_filename ();
    unsigned long line = get_line_number ();
    
    const char *source_line = 0;
    unsigned long caret_offset = 0;
    
    if (parse_qualified_name (&name, &wildcard, "identifier in import name", &source_line, &caret_offset)) {
    
        free (name);
        return 1;
    
    }
    
    add_import (names, name, wildcard, 0, filename, line, source_line, caret_offset);
    free (name);
    
    return 0;

}

static int parse_implements_clause (struct source_names *names) {

    for (;;) {
    
        char *iface_desc = 0;
        get_token ();
        
        if (parse_type_name (names, 0, &iface_desc)) {
            return 1;
        }
        
        add_interface (names, iface_desc);
        free (iface_desc);
        
        if (tok.kind != TOK_COMMA) {
            break;
        }
    
    }
    
    return 0;

}

static int parse_interface_member_decl (struct source_names *names) {

    char *type_desc;
    char *member_name;
    
    unsigned int flags = 0;
    
    for (;;) {
    
        if (skip_annotations ()) {
            return 1;
        }
        
        if (!is_member_modifier (tok.kind)) {
            break;
        }
        
        if (tok.kind == TOK_PUBLIC) {
        
            if (add_access_modifier_flag (&flags, METHOD_PUBLIC)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_PRIVATE) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "interface member cannot be private");
            return 1;
        
        } else if (tok.kind == TOK_PROTECTED) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "interface member cannot be protected");
            return 1;
        
        } else if (tok.kind == TOK_STATIC) {
        
            if (add_modifier_flag (&flags, METHOD_STATIC)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_FINAL) {
        
            if (add_modifier_flag (&flags, METHOD_FINAL)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_VOLATILE) {
        
            if (add_modifier_flag (&flags, FIELD_VOLATILE)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_TRANSIENT) {
        
            if (add_modifier_flag (&flags, FIELD_TRANSIENT)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_NATIVE) {
        
            if (add_modifier_flag (&flags, METHOD_NATIVE)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_SYNCHRONIZED) {
        
            if (add_modifier_flag (&flags, METHOD_SYNCHRONIZED)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_ABSTRACT) {
        
            if (add_modifier_flag (&flags, METHOD_ABSTRACT)) {
                return 1;
            }
        
        } else if (tok.kind == TOK_STRICTFP) {
        
            if (add_modifier_flag (&flags, METHOD_STRICTFP)) {
                return 1;
            }
        
        }
        
        get_token ();
    
    }
    
    if (parse_type_name (names, 1, &type_desc)) {
        return 1;
    }
    
    if (expect_ident ("interface member name")) {
    
        free (type_desc);
        return 1;
    
    }
    
    member_name = xstrdup (tok.ident);
    get_token ();
    
    if (tok.kind != TOK_LPAREN) {
    
        unsigned long array_depth = 0;
        
        if (append_array_suffix (&array_depth)) {
        
            free (member_name);
            free (type_desc);
            
            return 1;
        
        }
        
        type_desc = apply_array_suffix (type_desc, array_depth);
        
        if (type_desc[0] == 'V') {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "field cannot have void type");
            
            free (type_desc);
            free (member_name);
            
            return 1;
        
        }
        
        if (tok.kind == TOK_ASSIGN) {
        
            if (skip_field_initializer ()) {
            
                free (type_desc);
                free (member_name);
                
                return 1;
            
            }
        
        }
        
        if (expect_token (TOK_SEMI, "';'")) {
        
            free (type_desc);
            free (member_name);
            
            return 1;
        
        }
        
        add_field (names, member_name, type_desc, 0, 0, 0, 0, 0, METHOD_PUBLIC | METHOD_STATIC | METHOD_FINAL);
        
        free (type_desc);
        free (member_name);
        
        return 0;
    
    }
    
    {
    
        char *params;
        
        char **throws_types = 0;
        char **param_names = 0;
        
        unsigned long nb_throws_types = 0;
        unsigned long nb_param_names = 0;
        
        if (parse_param_list (names, &params, &param_names, &nb_param_names)) {
        
            free (member_name);
            free (type_desc);
            
            return 1;
        
        }
        
        get_token ();
        
        if (parse_throws_clause (names, &throws_types, &nb_throws_types)) {
        
            free (type_desc);
            
            free (params);
            free (member_name);
            
            free_param_names (param_names, nb_param_names);
            return 1;
        
        }
        
        if (flags & METHOD_STATIC) {
        
            if (expect_token (TOK_LBRACE, "'{'") ) {
            
                free (type_desc);
                free (params);
                free (member_name);
                
                free_param_names (param_names, nb_param_names);
                return 1;
            
            }
            
            add_method (names, member_name, type_desc, params, param_names, nb_param_names, flags);
            
            names->methods[names->nb_methods - 1].throws_types = throws_types;
            names->methods[names->nb_methods - 1].nb_throws_types = nb_throws_types;
            
            free (type_desc);
            free (params);
            free (member_name);
            
            return parse_method_body (names, &names->methods[names->nb_methods - 1]);
        
        }
        
        if (expect_token (TOK_SEMI, "';'") ) {
        
            free (type_desc);
            free (params);
            free (member_name);
            
            free_param_names (param_names, nb_param_names);
            return 1;
        
        }
        
        add_method (names, member_name, type_desc, params, param_names, nb_param_names, METHOD_PUBLIC | METHOD_ABSTRACT);
        
        names->methods[names->nb_methods - 1].throws_types = throws_types;
        names->methods[names->nb_methods - 1].nb_throws_types = nb_throws_types;
        
        free (type_desc);
        free (params);
        free (member_name);
        
        return 0;
    
    }

}

static int parse_interface_body (struct source_names *names) {

    int failed = 0;
    int depth;
    
    if (expect_token (TOK_LBRACE, "'{' after interface name")) {
        return 1;
    }
    
    get_token ();
    depth = get_token_brace_depth ();
    
    while (tok.kind != TOK_RBRACE) {
    
        if (tok.kind == TOK_EOF) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unexpected end of file in interface body");
            return 1;
        
        }
        
        if (parse_interface_member_decl (names)) {
        
            failed = 1;
            
            while (tok.kind != TOK_EOF) {
            
                int token_depth = get_token_brace_depth ();
                
                if (tok.kind == TOK_RBRACE && token_depth < depth) {
                    break;
                }
                
                if (token_depth == depth && (tok.kind == TOK_SEMI || tok.kind == TOK_RBRACE)) {
                
                    get_token ();
                    break;
                
                }
                
                get_token ();
            
            }
            
            continue;
        
        }
        
        get_token ();
    
    }
    
    return failed;

}

static int parse_interface_extends_clause (struct source_names *names) {

    for (;;) {
    
        char *iface_desc = 0;
        get_token ();
        
        if (parse_type_name (names, 0, &iface_desc)) {
            return 1;
        }
        
        add_interface (names, iface_desc);
        free (iface_desc);
        
        if (tok.kind != TOK_COMMA) {
            break;
        }
    
    }
    
    return 0;

}

static int parse_interface_decl (struct source_names *names) {

    get_token ();
    
    if (expect_ident ("interface name")) {
        return 1;
    }
    
    if (!names->class_name) {
        names->class_name = xstrdup (tok.ident);
    }
    
    names->is_interface = 1;
    get_token ();
    
    if (tok.kind == TOK_EXTENDS) {
    
        if (parse_interface_extends_clause (names)) {
            return 1;
        }
    
    }
    
    return parse_interface_body (names);

}

static int parse_class_decl (struct source_names *names) {

    int ret;
    get_token ();
    
    if (expect_ident ("class name")) {
        return 1;
    }
    
    if (!names->class_name) {
        names->class_name = xstrdup (tok.ident);
    }
    
    get_token ();
    
    if (tok.kind == TOK_EXTENDS) {
    
        char *super_desc = 0;
        get_token ();
        
        if (parse_type_name (names, 0, &super_desc)) {
            return 1;
        }
        
        if (names->super_class) {
            free (names->super_class);
        }
        
        names->super_class = super_desc;
    
    } else if (strcmp (names->class_name, "Object") != 0 || !names->package_name || strcmp (names->package_name, "java.lang") != 0) {
        names->super_class = xstrdup (JAVA_OBJECT_TYPE);
    }
    
    if (tok.kind == TOK_IMPLEMENTS) {
    
        if (parse_implements_clause (names)) {
            return 1;
        }
    
    }
    
    ret = parse_class_body (names);
    
    if (!ret && !names->is_interface && !has_constructor (names)) {
        add_method (names, "<init>", "V", "", 0, 0, METHOD_PUBLIC);
    }
    
    return ret;

}

static int scan_source_names (struct source_names *names) {

    memset (names, 0, sizeof (*names));
    get_token ();
    
    if (tok.kind == TOK_PACKAGE) {
    
        if (parse_package_decl (names)) {
            return 1;
        }
        
        get_token ();
    
    }
    
    while (tok.kind == TOK_IMPORT) {
    
        if (parse_import_decl (names)) {
            return 1;
        }
        
        get_token ();
    
    }
    
    while (tok.kind != TOK_EOF) {
    
        if (tok.kind == TOK_PACKAGE) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "package declaration must be first");
            return 1;
        
        }
        
        if (tok.kind == TOK_IMPORT) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "import declaration must appear before class declaration");
            return 1;
        
        }
        
        if (tok.kind == TOK_PUBLIC) {
        
            if (names->class_flags & CLASS_PUBLIC) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "duplicate modifier");
                return 1;
            
            }
            
            names->class_flags |= CLASS_PUBLIC;
            
            get_token ();
            continue;
        
        }
        
        if (tok.kind == TOK_PRIVATE || tok.kind == TOK_PROTECTED) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                tok.start, tok.caret, "top-level class or interface cannot be private or protected");
            return 1;
        
        }
        
        if (tok.kind == TOK_FINAL) {
        
            if (names->class_flags & CLASS_FINAL) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "duplicate modifier");
                return 1;
            
            }
            
            if (names->class_flags & CLASS_ABSTRACT) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "class cannot be both abstract and final");
                return 1;
            
            }
            
            names->class_flags |= CLASS_FINAL;
            
            get_token ();
            continue;
        
        }
        
        if (tok.kind == TOK_ABSTRACT) {
        
            if (names->class_flags & CLASS_ABSTRACT) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "duplicate modifier");
                return 1;
            
            }
            
            if (names->class_flags & CLASS_FINAL) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "class cannot be both abstract and final");
                return 1;
            
            }
            
            names->class_flags |= CLASS_ABSTRACT;
            
            get_token ();
            continue;
        
        }
        
        if (tok.kind == TOK_STRICTFP) {
        
            if (names->class_flags & CLASS_STRICTFP) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "duplicate modifier");
                return 1;
            
            }
            
            names->class_flags |= CLASS_STRICTFP;
            
            get_token ();
            continue;
        
        }
        
        if (tok.kind == TOK_CLASS) {
            return parse_class_decl (names);
        }
        
        if (tok.kind == TOK_INTERFACE) {
        
            if (names->class_flags & CLASS_FINAL) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                    tok.start, tok.caret, "interface cannot be final");
                return 1;
            
            }
            
            return parse_interface_decl (names);
        
        }
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected class or interface declaration");
        return 1;
    
    }
    
    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "expected class declaration");
    return 1;

}

static char *get_source_base_name (const char *src_path) {

    const char *base = src_path;
    const char *slash;
    const char *backslash;
    const char *dot;
    
    unsigned long len;
    char *name;
    
    slash = strrchr (src_path, '/');
    backslash = strrchr (src_path, '\\');
    
    if (slash && slash + 1 > base) {
        base = slash + 1;
    }
    
    if (backslash && backslash + 1 > base) {
        base = backslash + 1;
    }
    
    dot = strrchr (base, '.');
    
    if (dot) {
        len = (unsigned long) (dot - base);
    } else {
        len = strlen (base);
    }
    
    name = xmalloc (len + 1);
    
    memcpy (name, base, len);
    name[len] = '\0';
    
    return name;

}

static int check_class_filename (const char *src_path, const char *class_name) {

    char *file_class;
    int ret = 0;
    
    file_class = get_source_base_name (src_path);
    
    if (strcmp (file_class, class_name) != 0) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "class name '%s' does not match file name '%s'", class_name, file_class);
        ret = 1;
    
    }
    
    free (file_class);
    return ret;

}

struct dex_buf {

    unsigned char *data;
    unsigned long size;
    unsigned long cap;

};

struct dex_string_ref {

    const char *text;
    unsigned long index;

};

static void dex_reserve (struct dex_buf *buf, unsigned long extra) {

    unsigned long need = buf->size + extra;
    unsigned long cap = buf->cap ? buf->cap : 256;
    
    while (cap < need) {
        cap *= 2;
    }
    
    if (cap != buf->cap) {
    
        buf->data = xrealloc (buf->data, cap);
        buf->cap = cap;
    
    }

}

static unsigned long dex_pos (struct dex_buf *buf) {
    return buf->size;
}

static void dex_u1 (struct dex_buf *buf, unsigned int value) {

    dex_reserve (buf, 1);
    buf->data[buf->size++] = (unsigned char) value;

}

static void dex_u2_at (struct dex_buf *buf, unsigned long off, unsigned int value) {

    buf->data[off + 0] = (unsigned char) value;
    buf->data[off + 1] = (unsigned char) (value >> 8);

}

static void dex_u4_at (struct dex_buf *buf, unsigned long off, unsigned long value) {

    buf->data[off + 0] = (unsigned char) value;
    buf->data[off + 1] = (unsigned char) (value >> 8);
    buf->data[off + 2] = (unsigned char) (value >> 16);
    buf->data[off + 3] = (unsigned char) (value >> 24);

}

static void dex_u2 (struct dex_buf *buf, unsigned int value) {

    dex_reserve (buf, 2);
    
    dex_u2_at (buf, buf->size, value);
    buf->size += 2;

}

static void dex_u4 (struct dex_buf *buf, unsigned long value) {

    dex_reserve (buf, 4);
    
    dex_u4_at (buf, buf->size, value);
    buf->size += 4;

}

static void dex_bytes (struct dex_buf *buf, const void *data, unsigned long len) {

    dex_reserve (buf, len);
    
    memcpy (buf->data + buf->size, data, len);
    buf->size += len;

}

static void dex_align4 (struct dex_buf *buf) {

    while (buf->size & 3) {
        dex_u1 (buf, 0);
    }

}

static void dex_uleb128 (struct dex_buf *buf, unsigned long value) {

    do {
    
        unsigned int b = value & 0x7f;
        value >>= 7;
        
        if (value) {
            b |= 0x80;
        }
        
        dex_u1 (buf, b);
    
    } while (value);

}

static unsigned long dex_find_string (struct dex_string_ref *arr, unsigned long count, const char *text);

static void dex_encoded_unsigned (struct dex_buf *buf, unsigned int type, unsigned long low, unsigned long high, unsigned int width) {

    unsigned char bytes[8];
    unsigned int size = width, i;
    
    for (i = 0; i < width; i++) {
    
        if (i < 4) {
            bytes[i] = (unsigned char) ((low >> (i * 8)) & 0xff);
        } else {
            bytes[i] = (unsigned char) ((high >> ((i - 4) * 8)) & 0xff);
        }
    
    }
    
    while (size > 1 && bytes[size - 1] == 0) {
        size--;
    }
    
    dex_u1 (buf, type | ((size - 1) << 5));
    dex_bytes (buf, bytes, size);

}

static void dex_encoded_signed (struct dex_buf *buf, unsigned int type, unsigned long low, unsigned long high, unsigned int width) {

    unsigned char bytes[8];
    unsigned int size = width, i;
    
    for (i = 0; i < width; i++) {
    
        if (i < 4) {
            bytes[i] = (unsigned char) ((low >> (i * 8)) & 0xff);
        } else {
            bytes[i] = (unsigned char) ((high >> ((i - 4) * 8)) & 0xff);
        }
    
    }
    
    while (size > 1) {
    
        unsigned char top = bytes[size - 1];
        unsigned char next = bytes[size - 2];
        
        if ((top == 0x00 && !(next & 0x80)) || (top == 0xff && (next & 0x80))) {
            size--;
        } else {
            break;
        }
    
    }
    
    dex_u1 (buf, type | ((size - 1) << 5));
    dex_bytes (buf, bytes, size);

}

static void dex_write_encoded_field_value (struct dex_buf *buf, struct source_names *names, struct field_info *field, struct dex_string_ref *strings, unsigned long nb_strings) {

    int64_s value;
    (void) names;
    
    if (!field->has_initializer || !field->init_text) {
    
        if (field->type[0] == 'L' || field->type[0] == '[') {
            dex_u1 (buf, 0x1e);
        } else if (field->type[0] == 'Z') {
            dex_u1 (buf, 0x1f);
        } else {
        
            dex_u1 (buf, 0x04);
            dex_u1 (buf, 0);
        
        }
        
        return;
    
    }
    
    if ((field->type[0] == 'L' || field->type[0] == '[')) {
    
        dex_encoded_unsigned (buf, 0x17, dex_find_string (strings, nb_strings, field->init_text), 0, 4);
        return;
    
    }
    
    if (field->type[0] == 'F') {
    
        union { float f; unsigned long u; } bits;
        bits.f = (float) field_initializer_numeric_value (field->init_text);
        
        dex_encoded_unsigned (buf, 0x10, bits.u, 0, 4);
        return;
    
    }
    
    if (field->type[0] == 'D') {
    
        union { double d; unsigned char b[8]; } bits;
        unsigned long low, high;
        
        bits.d = field_initializer_numeric_value (field->init_text);
        
        low = (unsigned long) bits.b[0] | ((unsigned long) bits.b[1] << 8) | ((unsigned long) bits.b[2] << 16) | ((unsigned long) bits.b[3] << 24);
        high = (unsigned long) bits.b[4] | ((unsigned long) bits.b[5] << 8) | ((unsigned long) bits.b[6] << 16) | ((unsigned long) bits.b[7] << 24);
        
        dex_encoded_unsigned (buf, 0x11, low, high, 8);
        return;
    
    }
    
    parse_integer_text64 (&value, field->init_text);
    
    if (field->type[0] == 'J') {
        dex_encoded_signed (buf, 0x06, value.low, value.high, 8);
    } else if (field->type[0] == 'S') {
        dex_encoded_signed (buf, 0x02, value.low, value.high, 2);
    } else if (field->type[0] == 'C') {
        dex_encoded_unsigned (buf, 0x03, value.low, value.high, 2);
    } else if (field->type[0] == 'B') {
    
        dex_u1 (buf, 0x00);
        dex_u1 (buf, value.low & 0xff);
    
    } else if (field->type[0] == 'Z') {
        dex_u1 (buf, 0x1f | ((value.low != 0) << 5));
    } else {
        dex_encoded_signed (buf, 0x04, value.low, value.high, 4);
    }

}

static unsigned long dex_write_static_values (struct dex_buf *buf, struct source_names *names, unsigned long *field_id_idx, struct dex_string_ref *strings, unsigned long nb_strings) {

    unsigned long i;
    unsigned long id;
    unsigned long last = 0;
    
    int have_value = 0;
    
    for (i = 0; i < names->nb_fields; i++) {
    
        if ((names->fields[i].flags & METHOD_STATIC) && names->fields[i].has_initializer) {
        
            if (!have_value || field_id_idx[i] > last) {
                last = field_id_idx[i];
            }
            
            have_value = 1;
        
        }
    
    }
    
    if (!have_value) {
        return 0;
    }
    
    {
    
        unsigned long off = dex_pos (buf);
        unsigned long count = 0;
        
        for (id = 0; id <= last; id++) {
        
            for (i = 0; i < names->nb_fields; i++) {
            
                if ((names->fields[i].flags & METHOD_STATIC) && field_id_idx[i] == id) {
                
                    count++;
                    break;
                
                }
            
            }
        
        }
        
        dex_uleb128 (buf, count);
        
        for (id = 0; id <= last; id++) {
        
            for (i = 0; i < names->nb_fields; i++) {
            
                if ((names->fields[i].flags & METHOD_STATIC) && field_id_idx[i] == id) {
                
                    dex_write_encoded_field_value (buf, names, &names->fields[i], strings, nb_strings);
                    break;
                
                }
            
            }
        
        }
        
        return off;
    
    }

}

static char *dex_class_descriptor (struct source_names *names) {

    unsigned long len;
    
    char *desc;
    char *p;
    
    len = 2 + strlen (names->class_name) + 1;
    
    if (names->package_name) {
        len += strlen (names->package_name) + 1;
    }
    
    desc = xmalloc (len + 1);
    p = desc;
    
    *p++ = 'L';
    
    if (names->package_name && *names->package_name) {
    
        const char *q;
        
        for (q = names->package_name; *q; q++) {
            *p++ = (*q == '.') ? '/' : *q;
        }
        
        *p++ = '/';
    
    }
    
    strcpy (p, names->class_name);
    p += strlen (p);
    
    *p++ = ';';
    *p = '\0';
    
    return desc;

}

static int dex_string_cmp (const void *a, const void *b) {

    const struct dex_string_ref *sa = a;
    const struct dex_string_ref *sb = b;
    
    return strcmp (sa->text, sb->text);

}

static void dex_add_string (struct dex_string_ref **arr, unsigned long *count, const char *text) {

    unsigned long i;
    
    for (i = 0; i < *count; i++) {
    
        if (strcmp ((*arr)[i].text, text) == 0) {
            return;
        }
    
    }
    
    *arr = xrealloc (*arr, (*count + 1) * sizeof (**arr));
    
    (*arr)[*count].text = xstrdup (text);
    (*arr)[*count].index = 0;
    
    (*count)++;

}

static unsigned long dex_find_string (struct dex_string_ref *arr, unsigned long count, const char *text) {

    unsigned long i;
    
    for (i = 0; i < count; i++) {
    
        if (strcmp (arr[i].text, text) == 0) {
            return i;
        }
    
    }
    
    return 0xFFFFFFFFUL;

}

static int dex_type_order (const char *a, const char *b, struct dex_string_ref *strings, unsigned long nb_strings) {

    unsigned long ai = dex_find_string (strings, nb_strings, a);
    unsigned long bi = dex_find_string (strings, nb_strings, b);
    
    if (ai < bi) {
        return -1;
    }
    
    if (ai > bi) {
        return 1;
    }
    
    return 0;

}

static unsigned long dex_adler32 (const unsigned char *data, unsigned long len) {

    unsigned long a = 1, b = 0, i;
    
    for (i = 0; i < len; i++) {
    
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    
    }
    
    return (b << 16) | a;

}

struct sha1_ctx {

    unsigned int h[5];
    
    unsigned char block[64];
    unsigned long block_len;
    
    unsigned int total_hi;
    unsigned int total_lo;

};

static unsigned int sha1_rol (unsigned int v, int bits) {
    return (v << bits) | (v >> (32 - bits));
}

static void sha1_transform (struct sha1_ctx *ctx, const unsigned char *block) {

    unsigned int a, b, c, d, e, f, k, temp;
    
    unsigned int w[80];
    int i;
    
    for (i = 0; i < 16; i++) {
    
        w[i] = ((unsigned int) block[i * 4 + 0] << 24)
            | ((unsigned int) block[i * 4 + 1] << 16)
                | ((unsigned int) block[i * 4 + 2] << 8)
                    | ((unsigned int) block[i * 4 + 3]);
    
    }
    
    for (i = 16; i < 80; i++) {
        w[i] = sha1_rol (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    
    a = ctx->h[0]; b = ctx->h[1]; c = ctx->h[2]; d = ctx->h[3]; e = ctx->h[4];
    
    for (i = 0; i < 80; i++) {
    
        if (i < 20) {
        
            f = (b & c) | ((~b) & d);
            k = 0x5A827999UL;
        } else if (i < 40) {
        
            f = b ^ c ^ d;
            k = 0x6ED9EBA1UL;
        } else if (i < 60) {
        
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCUL;
        } else {
        
            f = b ^ c ^ d;
            k = 0xCA62C1D6UL;
        }
        
        temp = sha1_rol (a, 5) + f + e + k + w[i];
        e = d; d = c; c = sha1_rol (b, 30); b = a; a = temp;
    
    }
    
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d; ctx->h[4] += e;

}

static void sha1_init (struct sha1_ctx *ctx) {

    ctx->h[0] = 0x67452301UL; ctx->h[1] = 0xEFCDAB89UL; ctx->h[2] = 0x98BADCFEUL;
    ctx->h[3] = 0x10325476UL; ctx->h[4] = 0xC3D2E1F0UL;
    
    ctx->block_len = 0;
    ctx->total_hi = 0;
    ctx->total_lo = 0;

}

static void sha1_update (struct sha1_ctx *ctx, const unsigned char *data, unsigned long len) {

    unsigned int bit_len = (unsigned int) (len << 3);
    unsigned long i;
    
    ctx->total_lo += bit_len;
    
    if (ctx->total_lo < bit_len) {
        ctx->total_hi++;
    }
    
    ctx->total_hi += (unsigned int) (len >> 29);
    
    for (i = 0; i < len; i++) {
    
        ctx->block[ctx->block_len++] = data[i];
        
        if (ctx->block_len == 64) {
        
            sha1_transform (ctx, ctx->block);
            ctx->block_len = 0;
        
        }
    
    }

}

static void sha1_final (struct sha1_ctx *ctx, unsigned char out[20]) {

    int i;
    ctx->block[ctx->block_len++] = 0x80;
    
    if (ctx->block_len > 56) {
    
        while (ctx->block_len < 64) {
            ctx->block[ctx->block_len++] = 0;
        }
        
        sha1_transform (ctx, ctx->block);
        ctx->block_len = 0;
    
    }
    
    while (ctx->block_len < 56) {
        ctx->block[ctx->block_len++] = 0;
    }
    
    ctx->block[56] = (unsigned char) (ctx->total_hi >> 24);
    ctx->block[57] = (unsigned char) (ctx->total_hi >> 16);
    ctx->block[58] = (unsigned char) (ctx->total_hi >> 8);
    
    ctx->block[59] = (unsigned char) ctx->total_hi;
    
    ctx->block[60] = (unsigned char) (ctx->total_lo >> 24);
    ctx->block[61] = (unsigned char) (ctx->total_lo >> 16);
    ctx->block[62] = (unsigned char) (ctx->total_lo >> 8);
    
    ctx->block[63] = (unsigned char) ctx->total_lo;
    sha1_transform (ctx, ctx->block);
    
    for (i = 0; i < 5; i++) {
    
        out[i * 4 + 0] = (unsigned char) (ctx->h[i] >> 24);
        out[i * 4 + 1] = (unsigned char) (ctx->h[i] >> 16);
        out[i * 4 + 2] = (unsigned char) (ctx->h[i] >> 8);
        out[i * 4 + 3] = (unsigned char) ctx->h[i];
    
    }

}

static void dex_finish_hashes (struct dex_buf *buf) {

    struct sha1_ctx ctx;
    
    unsigned char digest[20];
    unsigned long checksum;
    
    sha1_init (&ctx);
    
    sha1_update (&ctx, buf->data + 32, buf->size - 32);
    sha1_final (&ctx, digest);
    
    memcpy (buf->data + 12, digest, 20);
    
    checksum = dex_adler32 (buf->data + 12, buf->size - 12);
    dex_u4_at (buf, 8, checksum);

}

static unsigned long count_descriptor_params (const char *p) {

    unsigned long count = 0;
    
    while (*p) {
    
        while (*p == '[') {
            p++;
        }
        
        if (*p == 'L') {
        
            while (*p && *p != ';') {
                p++;
            }
            
            if (*p == ';') {
                p++;
            }
        
        } else if (*p) {
            p++;
        }
        
        count++;
    
    }
    
    return count;

}

static unsigned long count_descriptor_registers (const char *p) {

    unsigned long count = 0;
    
    while (*p) {
    
        const char *start = p;
        
        while (*p == '[') {
            p++;
        }
        
        if (*p == 'L') {
        
            while (*p && *p != ';') {
                p++;
            }
            
            if (*p == ';') {
                p++;
            }
            
            count++;
        
        } else if (*p) {
        
            if (p == start && (*p == 'J' || *p == 'D')) {
                count += 2;
            } else {
                count++;
            }
            
            p++;
        
        }
    
    }
    
    return count;

}

static unsigned long find_type_idx (const char **types, unsigned long nb_types, const char *desc) {

    unsigned long i;
    
    for (i = 0; i < nb_types; i++) {
    
        if (strcmp (types[i], desc) == 0) {
            return i;
        }
    
    }
    
    return METHOD_NOT_FOUND;

}

static unsigned int dex_invoke_regs2 (unsigned int r0, unsigned int r1) {
    return (r0 & 0x0F) | ((r1 & 0x0F) << 4);
}

static unsigned int dex_invoke_regs4 (unsigned int r0, unsigned int r1, unsigned int r2, unsigned int r3) {
    return (r0 & 0x0F) | ((r1 & 0x0F) << 4) | ((r2 & 0x0F) << 8) | ((r3 & 0x0F) << 12);
}

static const char *statement_target_caret (struct statement_info *stmt);

static int method_params_match_int_widening (const char *actual, const char *formal) {

    const char *a = actual;
    const char *f = formal;
    
    while (*a && *f) {
    
        if (*a == '[' || *f == '[' || *a == 'L' || *f == 'L') {
        
            const char *a_start = a;
            const char *f_start = f;
            
            while (*a == '[') {
                a++;
            }
            
            while (*f == '[') {
                f++;
            }
            
            if (*a == 'L') {
            
                while (*a && *a != ';') {
                    a++;
                }
                
                if (*a == ';') {
                    a++;
                }
            
            } else if (*a) {
                a++;
            }
            
            if (*f == 'L') {
            
                while (*f && *f != ';') {
                    f++;
                }
                
                if (*f == ';') {
                    f++;
                }
            
            } else if (*f) {
                f++;
            }
            
            if (a - a_start != f - f_start || memcmp (a_start, f_start, a - a_start) != 0) {
                return 0;
            }
        
        } else {
        
            if (*a != *f && !((*a == 'B' || *a == 'S' || *a == 'C') && *f == 'I')) {
                return 0;
            }
            
            a++;
            f++;
        
        }
    
    }
    
    return !*a && !*f;

}

static unsigned long find_static_method_id_single_params (struct source_names *names, unsigned long *method_id_idx, const char *target, const char *name, const char *params) {

    unsigned long i;
    
    if (!target || strcmp (target, names->class_name) != 0) {
        return METHOD_NOT_FOUND;
    }
    
    for (i = 0; i < names->nb_methods; i++) {
    
        if (strcmp (names->methods[i].java_name, name) == 0 && strcmp (names->methods[i].params, params) == 0) {
            return method_id_idx[i];
        }
    
    }
    
    for (i = 0; i < names->nb_methods; i++) {
    
        if (strcmp (names->methods[i].java_name, name) == 0 && method_params_match_int_widening (params, names->methods[i].params)) {
            return method_id_idx[i];
        }
    
    }
    
    return METHOD_NOT_FOUND;

}

static unsigned long find_static_method_id_single (struct source_names *names, unsigned long *method_id_idx, const char *target, const char *name) {

    unsigned long i;
    
    if (!target || strcmp (target, names->class_name) != 0) {
        return 0xFFFFFFFFUL;
    }
    
    for (i = 0; i < names->nb_methods; i++) {
    
        if (strcmp (names->methods[i].java_name, name) == 0) {
            return method_id_idx[i];
        }
    
    }
    
    return 0xFFFFFFFFUL;

}

static unsigned long find_static_method_id_single_typed (struct source_names *names, unsigned long *method_id_idx, const char *target, const char *name, const char *return_type) {

    unsigned long i;
    
    if (!target || strcmp (target, names->class_name) != 0) {
        return METHOD_NOT_FOUND;
    }
    
    for (i = 0; i < names->nb_methods; i++) {
    
        if (strcmp (names->methods[i].java_name, name) == 0 && strcmp (names->methods[i].return_type, return_type) == 0) {
            return method_id_idx[i];
        }
    
    }
    
    return METHOD_NOT_FOUND;

}

static void dex_write_code_item_header_ex (struct dex_buf *buf, unsigned int registers_size, unsigned int ins_size, unsigned int outs_size, unsigned int tries_size, unsigned long insns_size) {

    dex_u2 (buf, registers_size);
    dex_u2 (buf, ins_size);
    dex_u2 (buf, outs_size);
    dex_u2 (buf, tries_size);
    dex_u4 (buf, 0);
    dex_u4 (buf, insns_size);

}

static void dex_write_code_item_header (struct dex_buf *buf, unsigned int registers_size, unsigned int ins_size, unsigned int outs_size, unsigned long insns_size) {
    dex_write_code_item_header_ex (buf, registers_size, ins_size, outs_size, 0, insns_size);
}

static unsigned long method_local_slot_count (struct method_info *method) {

    unsigned long i;
    unsigned long count = 0;
    
    for (i = 0; i < method->nb_locals; i++) {
        count += (method->locals[i].type[0] == 'J' || method->locals[i].type[0] == 'D') ? 2 : 1;
    }
    
    return count;

}

static unsigned long method_local_register (struct method_info *method, unsigned long local_index) {

    unsigned long i;
    unsigned long reg = 6;
    
    for (i = 0; i < local_index; i++) {
        reg += (method->locals[i].type[0] == 'J' || method->locals[i].type[0] == 'D') ? 2 : 1;
    }
    
    return reg;

}

static unsigned long count_instance_result_call_move_units (struct method_info *method, struct statement_info *stmt) {

    unsigned long *arg_locals = 0, nb_arg_locals = 0, ai;
    unsigned long regs[5], reg_count = 0;
    
    char reg_types[5] = { 0, 0, 0, 0, 0 };
    unsigned long extra_units = 0;
    
    int needs_low_registers = 0;
    char *params = 0;
    
    if (stmt->kind != STMT_CALL || stmt->arg_kind != ARG_RESULT_MULTI || !stmt->target || stmt->target[0] != '$') {
        return 0;
    }
    
    if (split_constructor_arguments (stmt->arg_text, &params, &arg_locals, &nb_arg_locals)) {
        return 0;
    }
    
    reg_types[reg_count] = 'L';
    regs[reg_count++] = method_local_register (method, strtoul (stmt->target + 1, 0, 10));
    
    for (ai = 0; ai < nb_arg_locals && reg_count < 5; ai++) {
    
        const char *type = method->locals[arg_locals[ai]].type;
        unsigned long reg = method_local_register (method, arg_locals[ai]);
        
        if ((type[0] == 'J' || type[0] == 'D') && reg_count >= 4) {
            break;
        }
        
        reg_types[reg_count] = type[0];
        regs[reg_count++] = reg;
        
        if (type[0] == 'J' || type[0] == 'D') {
        
            reg_types[reg_count] = 0;
            regs[reg_count++] = reg + 1;
        
        }
    
    }
    
    for (ai = 0; ai < reg_count; ai++) {
    
        if (regs[ai] > 15) {
        
            needs_low_registers = 1;
            break;
        
        }
    
    }
    
    if (needs_low_registers) {
    
        for (ai = 0; ai < reg_count; ai++) {
        
            if (reg_types[ai]) {
                extra_units += regs[ai] > 15 ? 2 : 1;
            }
        
        }
    
    }
    
    free (arg_locals);
    free (params);
    
    return extra_units;

}

static const char *find_field_type (struct source_names *names, const char *field_name);
static const char *method_param_type (struct method_info *method, unsigned long param_index);

static const char *arithmetic_source_type (struct source_names *names, struct method_info *method, const char *source) {

    if (field_initializer_is_call (source)) {
        return "D";
    }
    
    if (source[0] == '#') {
        return method_param_type (method, strtoul (source + 1, 0, 10));
    }
    
    if (source[0] == '$') {
        return method->locals[strtoul (source + 1, 0, 10)].type;
    }
    
    return find_field_type (names, source);

}

static unsigned long arithmetic_source_load_units (const char *source) {

    if (field_initializer_is_call (source)) {
        return 4;
    }
    
    return source[0] == '#' || source[0] == '$' ? 1 : 2;

}

static const char *method_param_type (struct method_info *method, unsigned long param_index) {

    const char *p = method->params;
    unsigned long index = 0;
    
    while (*p && index < param_index) {
    
        while (*p == '[') {
            p++;
        }
        
        if (*p == 'L') {
        
            while (*p && *p != ';') {
                p++;
            }
            
            if (*p == ';') {
                p++;
            }
        
        } else if (*p) {
            p++;
        }
        
        index++;
    
    }
    
    return p;

}

static unsigned long method_param_register (struct method_info *method, unsigned long registers_size, unsigned long ins_size, unsigned long param_index) {

    const char *p = method->params;
    
    unsigned long reg = registers_size - ins_size;
    unsigned long index = 0;
    
    if (!(method->flags & METHOD_STATIC)) {
        reg++;
    }
    
    while (*p && index < param_index) {
    
        const char *start = p;
        
        while (*p == '[') {
            p++;
        }
        
        if (*p == 'L') {
        
            while (*p && *p != ';') {
                p++;
            }
            
            if (*p == ';') {
                p++;
            }
            
            reg++;
        
        } else if (*p) {
        
            if (p == start && (*p == 'J' || *p == 'D')) {
                reg += 2;
            } else {
                reg++;
            }
            
            p++;
        
        }
        
        index++;
    
    }
    
    return reg;

}

static unsigned long count_arithmetic_code_units (struct method_info *method, const char *source_type, const char *operations) {

    const char *p = operations;
    
    char current_type = source_type[0];
    unsigned long count = 0;
    
    if ((current_type == '[' || current_type == 'L') && (!p || !*p || strcmp (p, "1") == 0)) {
        return 0;
    }
    
    while (p && *p) {
    
        const char *end = strchr (p, '*');
        
        const char *factor = p;
        int operation = TOK_STAR;
        
        char factor_type;
        char operation_type;
        
        unsigned long len = end ? (unsigned long) (end - p) : strlen (p);
        
        if (len > 0 && factor[0] == '~') {
        
            factor++;
            len--;
        
        }
        
        if (len > 0 && strchr ("+-/%<>u&^|=qleghao", factor[0])) {
        
            if (factor[0] == '+') {
                operation = TOK_PLUS;
            } else if (factor[0] == '-') {
                operation = TOK_MINUS;
            } else if (factor[0] == '/') {
                operation = TOK_BSLASH;
            } else if (factor[0] == '%') {
                operation = TOK_MOD;
            } else if (factor[0] == '<') {
                operation = TOK_LSH;
            } else if (factor[0] == '>') {
                operation = TOK_RSH;
            } else if (factor[0] == 'u') {
                operation = TOK_URSH;
            } else if (factor[0] == '&') {
                operation = TOK_AMPER;
            } else if (factor[0] == '^') {
                operation = TOK_CARET;
            } else if (factor[0] == '|') {
                operation = TOK_PIPE;
            } else if (factor[0] == '=') {
                operation = TOK_EQEQ;
            } else if (factor[0] == 'q') {
                operation = TOK_NOTEQ;
            } else if (factor[0] == 'l') {
                operation = TOK_LESS;
            } else if (factor[0] == 'e') {
                operation = TOK_LTEQ;
            } else if (factor[0] == 'g') {
                operation = TOK_GREATER;
            } else if (factor[0] == 'h') {
                operation = TOK_GTEQ;
            } else if (factor[0] == 'a') {
                operation = TOK_LOGAND;
            } else if (factor[0] == 'o') {
                operation = TOK_LOGOR;
            }
            
            factor++;
            len--;
        
        }
        
        if (len == 2 && factor[0] == '!') {
        
            if (current_type != factor[1]) {
                count++;
            }
            
            current_type = factor[1];
        
        } else if (len == 1 && (factor[0] == 'N' || factor[0] == 'B')) {
            count++;
        } else if (len == 1 && factor[0] == 'z') {
        
            count += 2;
            current_type = 'I';
        
        } else {
        
            if (len > 1 && factor[0] == '$') {
                factor_type = method->locals[strtoul (factor + 1, 0, 10)].type[0];
            } else {
            
                char *text = xmalloc (len + 1);
                
                memcpy (text, factor, len);
                text[len] = '\0';
                
                factor_type = arithmetic_literal_type (text);
                free (text);
            
            }
            
            operation_type = (current_type == 'L' || current_type == '[')
                && (operation == TOK_EQEQ || operation == TOK_NOTEQ)
                    ? current_type : promoted_numeric_type (current_type, factor_type, operation);
            
            if (operation_type != current_type) {
            
                count++;
                current_type = operation_type;
            
            }
            
            if (len > 1 && factor[0] == '$') {
                count += 3;
            } else {
            
                int long_shift = current_type == 'J'
                    && (operation == TOK_LSH || operation == TOK_RSH || operation == TOK_URSH);
                
                count += long_shift ? 5 : (current_type == 'J' || current_type == 'D') ? 7 : 5;
            
            }
        
        }
        
        if (!end) {
            break;
        }
        
        p = end + 1;
    
    }
    
    return count + (current_type != source_type[0] ? 1 : 0);

}

static unsigned int dex_move_result_opcode (const char *return_type, unsigned long reg) {

    unsigned int op = 0x0a;
    
    if (return_type[0] == 'J' || return_type[0] == 'D') {
        op = 0x0b;
    } else if (return_type[0] == 'L' || return_type[0] == '[') {
        op = 0x0c;
    }
    
    return op | ((unsigned int) reg << 8);

}

static unsigned int dex_return_opcode (const char *return_type, unsigned long reg) {

    unsigned int op = 0x0f;
    
    if (return_type[0] == 'J' || return_type[0] == 'D') {
        op = 0x10;
    } else if (return_type[0] == 'L' || return_type[0] == '[') {
        op = 0x11;
    }
    
    return op | ((unsigned int) reg << 8);

}

static void dex_write_const_integer (struct dex_buf *buf, const char *return_type, unsigned long reg, const char *text) {

    int64_s value;
    
    if (return_type[0] == 'F') {
    
        union { float f; unsigned long u; } bits;
        bits.f = (float) field_initializer_numeric_value (text);
        
        dex_u2 (buf, 0x0014 | ((unsigned int) reg << 8));
        dex_u2 (buf, (unsigned int) (bits.u & 0xffff));
        dex_u2 (buf, (unsigned int) ((bits.u >> 16) & 0xffff));
        
        return;
    
    }
    
    if (return_type[0] == 'D') {
    
        union { double d; unsigned char b[8]; } bits;
        unsigned long lo, hi;
        
        bits.d = field_initializer_numeric_value (text);
        lo = (unsigned long) bits.b[0] | ((unsigned long) bits.b[1] << 8) | ((unsigned long) bits.b[2] << 16) | ((unsigned long) bits.b[3] << 24);
        hi = (unsigned long) bits.b[4] | ((unsigned long) bits.b[5] << 8) | ((unsigned long) bits.b[6] << 16) | ((unsigned long) bits.b[7] << 24);
        
        dex_u2 (buf, 0x0018 | ((unsigned int) reg << 8));
        dex_u2 (buf, (unsigned int) (lo & 0xffff));
        dex_u2 (buf, (unsigned int) ((lo >> 16) & 0xffff));
        dex_u2 (buf, (unsigned int) (hi & 0xffff));
        dex_u2 (buf, (unsigned int) ((hi >> 16) & 0xffff));
        
        return;
    
    }
    
    parse_integer_text64 (&value, text);
    
    if (return_type[0] == 'J') {
    
        dex_u2 (buf, 0x0018 | ((unsigned int) reg << 8));
        dex_u2 (buf, (unsigned int) (value.low & 0xffff));
        dex_u2 (buf, (unsigned int) ((value.low >> 16) & 0xffff));
        dex_u2 (buf, (unsigned int) (value.high & 0xffff));
        dex_u2 (buf, (unsigned int) ((value.high >> 16) & 0xffff));
        
        return;
    
    }
    
    dex_u2 (buf, 0x0014 | ((unsigned int) reg << 8));
    dex_u2 (buf, (unsigned int) (value.low & 0xffff));
    dex_u2 (buf, (unsigned int) ((value.low >> 16) & 0xffff));

}

static int field_init_supported (struct source_names *names, const char *type_desc) {

    (void) names;
    return type_desc[0] == '[' || type_desc[0] == 'Z' || type_desc[0] == 'I' || type_desc[0] == 'C' || type_desc[0] == 'S' || type_desc[0] == 'J' || type_desc[0] == 'F' || type_desc[0] == 'D' || type_desc[0] == 'L';

}

static unsigned int dex_aget_opcode_for_type (const char *type_desc) {

    switch (type_desc[0]) {
    
        case 'D':       case 'J':
        
            return 0x45;
        
        case 'L':       case '[':
        
            return 0x46;
        
        case 'Z':
        
            return 0x47;
        
        case 'B':
        
            return 0x48;
        
        case 'C':
        
            return 0x49;
        
        case 'S':
        
            return 0x4a;
        
        default:
        
            return 0x44;
    
    }

}

static unsigned int dex_aput_opcode_for_type (const char *type_desc) {

    switch (type_desc[0]) {
    
        case 'D':       case 'J':
        
            return 0x4c;
        
        case 'L':       case '[':
        
            return 0x4d;
        
        case 'Z':
        
            return 0x4e;
        
        case 'B':
        
            return 0x4f;
        
        case 'C':
        
            return 0x50;
        
        case 'S':
        
           return 0x51;
        
        default:
        
            return 0x4b;
    
    }

}

static unsigned int dex_iput_opcode_for_type (const char *type_desc) {

    switch (type_desc[0]) {
    
        case 'D':       case 'J':
        
            return 0x5a;
        
        case 'L':       case '[':
        
            return 0x5b;
        
        case 'Z':
        
            return 0x5c;
        
         case 'C':
        
            return 0x5e;
        
        case 'S':
        
            return 0x5f;
        
        default:
        
            return 0x59;
    
    }

}

static unsigned int dex_iget_opcode_for_type (const char *type_desc) {

    switch (type_desc[0]) {
    
        case 'D':       case 'J':
        
            return 0x53;
        
        case 'L':       case '[':
        
            return 0x54;
        
        case 'Z':
        
            return 0x55;
        
         case 'C':
        
            return 0x57;
        
        case 'S':
        
            return 0x58;
        
        default:
        
            return 0x52;
    
    }

}

static unsigned int dex_sget_opcode_for_type (const char *type_desc) {

    switch (type_desc[0]) {
    
        case 'D':       case 'J':
        
            return 0x61;
        
        case 'L':       case '[':
        
            return 0x62;
        
        case 'Z':
        
            return 0x63;
        
         case 'C':
        
            return 0x65;
        
        case 'S':
        
            return 0x66;
        
        default:
        
            return 0x60;
    
    }

}

static unsigned int dex_sput_opcode_for_type (const char *type_desc) {

    switch (type_desc[0]) {
    
        case 'D':       case 'J':
        
            return 0x68;
        
        case 'L':       case '[':
        
            return 0x69;
        
        case 'Z':
        
            return 0x6a;
        
         case 'C':
        
            return 0x6c;
        
        case 'S':
        
            return 0x6d;
        
        default:
        
            return 0x67;
    
    }

}

static unsigned long count_instance_field_initializer_code_units (struct source_names *names) {

    unsigned long i, count = 0;
    
    for (i = 0; i < names->nb_fields; i++) {
    
        if ((names->fields[i].flags & METHOD_STATIC) || !names->fields[i].has_initializer) {
            continue;
        }
        
        if (names->fields[i].is_constructor_initializer) {
            count += names->fields[i].init_text[5] == 'S' ? 9 : 7;
        } else if (field_initializer_is_array (names->fields[i].type, names->fields[i].init_text)) {
            count += 5 + field_array_count (names->fields[i].init_text) * 5;
        } else if (field_initializer_is_mul_di (names->fields[i].init_text)) {
            count += 13;
        } else if (field_initializer_is_call (names->fields[i].init_text)) {
            count += 6;
        } else if ((names->fields[i].type[0] == 'L')) {
            count += 4;
        } else if (names->fields[i].type[0] == 'J' || names->fields[i].type[0] == 'D') {
            count += 7;
        } else {
            count += 5;
        }
    
    }
    
    return count;

}

struct external_method_info {

    char *class_desc;
    char *name;
    char *return_type;
    char *params;
    char *shorty;
    
    unsigned long class_type_idx;
    unsigned long proto_idx;
    unsigned long name_idx;
    unsigned long method_id_idx;
    unsigned long params_off;

};

static unsigned long find_external_method_id (struct external_method_info *methods, unsigned long nb_methods, const char *class_desc, const char *name, const char *return_type, const char *params);

static void dex_write_int_to_double (struct dex_buf *buf, unsigned long dst_reg, unsigned long src_reg) {
    dex_u2 (buf, 0x0083 | ((unsigned int) dst_reg << 8) | ((unsigned int) src_reg << 12));
}

static struct external_method_info *find_external_method (struct external_method_info *methods, unsigned long nb_methods, const char *class_desc, const char *name, const char *params) {

    unsigned long i;
    
    for (i = 0; i < nb_methods; i++) {
    
        if (strcmp (methods[i].class_desc, class_desc) == 0
            && strcmp (methods[i].name, name) == 0
                && strcmp (methods[i].params, params) == 0) {
            return &methods[i];
        }
    
    }
    
    return 0;

}

static void dex_write_double_to_int (struct dex_buf *buf, unsigned long dst_reg, unsigned long src_reg) {
    dex_u2 (buf, 0x008a | ((unsigned int) dst_reg << 8) | ((unsigned int) src_reg << 12));
}

static void dex_write_mul_double (struct dex_buf *buf, unsigned long dst_reg, unsigned long left_reg, unsigned long right_reg) {

    dex_u2 (buf, 0x00ad | ((unsigned int) dst_reg << 8));
    dex_u2 (buf, (unsigned int) left_reg | ((unsigned int) right_reg << 8));

}

static unsigned long dex_move_value_code_units (unsigned long dst_reg, unsigned long src_reg) {
    return dst_reg < 16 && src_reg < 16 ? 1 : dst_reg < 256 ? 2 : 3;
}

static void dex_write_move_value (struct dex_buf *buf, char type, unsigned long dst_reg, unsigned long src_reg) {

    unsigned int opcode = (type == 'J' || type == 'D') ? 0x0004
        : (type == 'L' || type == '[') ? 0x0007 : 0x0001;
    
    if (dst_reg < 16 && src_reg < 16) {
        dex_u2 (buf, opcode | ((unsigned int) dst_reg << 8) | ((unsigned int) src_reg << 12));
    } else if (dst_reg < 256) {
    
        dex_u2 (buf, (opcode + 1) | ((unsigned int) dst_reg << 8));
        dex_u2 (buf, (unsigned int) src_reg);
    
    } else {
    
        dex_u2 (buf, opcode + 2);
        
        dex_u2 (buf, (unsigned int) dst_reg);
        dex_u2 (buf, (unsigned int) src_reg);
    
    }

}

static void dex_write_numeric_conversion (struct dex_buf *buf, char from_type, char to_type, unsigned long dst_reg, unsigned long src_reg) {

    unsigned int opcode = 0;
    
    if (from_type == to_type) {
    
        dex_write_move_value (buf, to_type, dst_reg, src_reg);
        return;
    
    }
    
    if (from_type == 'J' && to_type == 'I') {
        opcode = 0x84;
    } else if (from_type == 'J' && to_type == 'F') {
        opcode = 0x85;
    } else if (from_type == 'J' && to_type == 'D') {
        opcode = 0x86;
    } else if (from_type == 'F' && to_type == 'I') {
        opcode = 0x87;
    } else if (from_type == 'F' && to_type == 'J') {
        opcode = 0x88;
    } else if (from_type == 'F' && to_type == 'D') {
        opcode = 0x89;
    } else if (from_type == 'D' && to_type == 'I') {
        opcode = 0x8a;
    } else if (from_type == 'D' && to_type == 'J') {
        opcode = 0x8b;
    } else if (from_type == 'D' && to_type == 'F') {
        opcode = 0x8c;
    } else if (to_type == 'J') {
        opcode = 0x81;
    } else if (to_type == 'F') {
        opcode = 0x82;
    } else if (to_type == 'D') {
        opcode = 0x83;
    }
    
    if (opcode) {
        dex_u2 (buf, opcode | ((unsigned int) dst_reg << 8) | ((unsigned int) src_reg << 12));
    } else {
        dex_write_move_value (buf, to_type, dst_reg, src_reg);
    }

}

static unsigned int dex_binary_opcode_for_type (char type, int operation) {

    if (type == 'J') {
    
        if (operation == TOK_PLUS) {
            return 0x9b;
        }
        
        if (operation == TOK_MINUS) {
            return 0x9c;
        }
        
        if (operation == TOK_STAR) {
            return 0x9d;
        }
        
        if (operation == TOK_BSLASH) {
            return 0x9e;
        }
        
        if (operation == TOK_MOD) {
            return 0x9f;
        }
        
        if (operation == TOK_AMPER) {
            return 0xa0;
        }
        
        if (operation == TOK_PIPE) {
            return 0xa1;
        }
        
        if (operation == TOK_CARET) {
            return 0xa2;
        }
        
        if (operation == TOK_LSH) {
            return 0xa3;
        }
        
        if (operation == TOK_RSH) {
            return 0xa4;
        }
        
        if (operation == TOK_URSH) {
            return 0xa5;
        }
    
    } else if (type == 'F') {
    
        if (operation == TOK_PLUS) {
            return 0xa6;
        }
        
        if (operation == TOK_MINUS) {
            return 0xa7;
        }
        
        if (operation == TOK_STAR) {
            return 0xa8;
        }
        
        if (operation == TOK_BSLASH) {
            return 0xa9;
        }
        
        if (operation == TOK_MOD) {
            return 0xaa;
        }
    
    } else if (type == 'D') {
    
        if (operation == TOK_PLUS) {
            return 0xab;
        }
        
        if (operation == TOK_MINUS) {
            return 0xac;
        }
        
        if (operation == TOK_STAR) {
            return 0xad;
        }
        
        if (operation == TOK_BSLASH) {
            return 0xae;
        }
        
        if (operation == TOK_MOD) {
            return 0xaf;
        }
    
    } else {
    
        if (operation == TOK_PLUS) {
            return 0x90;
        }
        
        if (operation == TOK_MINUS) {
            return 0x91;
        }
        
        if (operation == TOK_STAR) {
            return 0x92;
        }
        
        if (operation == TOK_BSLASH) {
            return 0x93;
        }
        
        if (operation == TOK_MOD) {
            return 0x94;
        }
        
        if (operation == TOK_AMPER) {
            return 0x95;
        }
        
        if (operation == TOK_PIPE) {
            return 0x96;
        }
        
        if (operation == TOK_CARET) {
            return 0x97;
        }
        
        if (operation == TOK_LSH) {
            return 0x98;
        }
        
        if (operation == TOK_RSH) {
            return 0x99;
        }
        
        if (operation == TOK_URSH) {
            return 0x9a;
        }
    
    }
    
    return 0;

}

static void dex_write_mul_factor_list (struct dex_buf *buf, struct method_info *method, const char *source_type, const char *field_type, unsigned long tmp_reg, const char *factors) {

    const char *p = factors;
    char current_type = source_type[0];
    
    if ((current_type == 'L' || current_type == '[') && (field_type[0] == 'L' || field_type[0] == '[') && (!*p || strcmp (p, "1") == 0)) {
        return;
    }
    
    while (*p) {
    
        const char *end = strchr (p, '*');
        unsigned long len = end ? (unsigned long) (end - p) : strlen (p);
        
        char *factor = xmalloc (len + 1);
        
        int reverse_operation = 0;
        int operation = TOK_STAR;
        
        int runtime_local = 0;
        unsigned long source_reg = 0;
        
        char factor_type = current_type;
        unsigned long rhs_reg = tmp_reg + 2;
        
        memcpy (factor, p, len);
        factor[len] = '\0';
        
        if (factor[0] == '~') {
        
            reverse_operation = 1;
            memmove (factor, factor + 1, strlen (factor));
        
        }
        
        if (factor[0] == '+') {
        
            operation = TOK_PLUS;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == '-') {
        
            operation = TOK_MINUS;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == '/') {
        
            operation = TOK_BSLASH;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == '%') {
        
            operation = TOK_MOD;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == '<') {
        
            operation = TOK_LSH;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == '>') {
        
            operation = TOK_RSH;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == 'u') {
        
            operation = TOK_URSH;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == '&') {
        
            operation = TOK_AMPER;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == '^') {
        
            operation = TOK_CARET;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == '|') {
        
            operation = TOK_PIPE;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == '=') {
        
            operation = TOK_EQEQ;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == 'q') {
        
            operation = TOK_NOTEQ;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == 'l') {
        
            operation = TOK_LESS;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == 'e') {
        
            operation = TOK_LTEQ;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == 'g') {
        
            operation = TOK_GREATER;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == 'h') {
        
            operation = TOK_GTEQ;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == 'a') {
        
            operation = TOK_LOGAND;
            memmove (factor, factor + 1, strlen (factor));
        
        } else if (factor[0] == 'o') {
        
            operation = TOK_LOGOR;
            memmove (factor, factor + 1, strlen (factor));
        
        }
        
        if (strcmp (factor, "N") == 0) {
        
            unsigned int opcode = current_type == 'J' ? 0x7d : current_type == 'F' ? 0x7f : current_type == 'D' ? 0x80 : 0x7b;
            dex_u2 (buf, opcode | ((unsigned int) tmp_reg << 8) | ((unsigned int) tmp_reg << 12));
            
            free (factor);
            
            if (!end) {
                break;
            }
            
            p = end + 1;
            continue;
        
        }
        
        if (strcmp (factor, "B") == 0) {
        
            unsigned int opcode = current_type == 'J' ? 0x7e : 0x7c;
            dex_u2 (buf, opcode | ((unsigned int) tmp_reg << 8) | ((unsigned int) tmp_reg << 12));
            
            free (factor);
            
            if (!end) {
                break;
            }
            
            p = end + 1;
            continue;
        
        }
        
        if (strcmp (factor, "z") == 0) {
        
            if (current_type != 'I') {
                dex_write_numeric_conversion (buf, current_type, 'I', tmp_reg, tmp_reg);
            }
            
            dex_u2 (buf, 0x00d7 | ((unsigned int) tmp_reg << 8));
            dex_u2 (buf, (unsigned int) tmp_reg | (1U << 8));
            
            current_type = 'I';
            free (factor);
            
            if (!end) {
                break;
            }
            
            p = end + 1;
            continue;
        
        }
        
        if (factor[0] == '!' && factor[1] && !factor[2]) {
        
            if (current_type != factor[1]) {
                dex_write_numeric_conversion (buf, current_type, factor[1], tmp_reg, tmp_reg);
            }
            
            current_type = factor[1];
            free (factor);
            
            if (!end) {
                break;
            }
            
            p = end + 1;
            continue;
        
        }
        
        if (factor[0] == '$') {
        
            unsigned long local_index = strtoul (factor + 1, 0, 10);
            runtime_local = 1;
            
            source_reg = method_local_register (method, local_index);
            factor_type = method->locals[local_index].type[0];
        
        } else {
            factor_type = arithmetic_literal_type (factor);
        }
        
        {
        
            char operation_type = (current_type == 'L' || current_type == '[')
                && (operation == TOK_EQEQ || operation == TOK_NOTEQ)
                    ? current_type : promoted_numeric_type (current_type, factor_type, operation);
            
            if (operation_type != current_type) {
            
                dex_write_numeric_conversion (buf, current_type, operation_type, tmp_reg, tmp_reg);
                current_type = operation_type;
            
            }
        
        }
        
        if (runtime_local) {
        
            if ((operation == TOK_LSH || operation == TOK_RSH || operation == TOK_URSH) && current_type == 'J') {
                dex_write_numeric_conversion (buf, factor_type, 'I', rhs_reg, source_reg);
            } else {
                dex_write_numeric_conversion (buf, factor_type, current_type, rhs_reg, source_reg);
            }
        
        } else {
        
            if ((operation == TOK_LSH || operation == TOK_RSH || operation == TOK_URSH) && current_type == 'J') {
                dex_write_const_integer (buf, "I", rhs_reg, factor);
            } else {
            
                char const_type[2];
                
                const_type[0] = current_type;
                const_type[1] = '\0';
                
                dex_write_const_integer (buf, const_type, rhs_reg, factor);
            
            }
        
        }
        
        {
        
            unsigned long left_reg = reverse_operation ? rhs_reg : tmp_reg;
            unsigned long right_reg = reverse_operation ? tmp_reg : rhs_reg;
            
            if (operation == TOK_LOGAND || operation == TOK_LOGOR) {
            
                unsigned int opcode = operation == TOK_LOGAND ? 0x95 : 0x96;
                
                dex_u2 (buf, opcode | ((unsigned int) tmp_reg << 8));
                dex_u2 (buf, (unsigned int) left_reg | ((unsigned int) right_reg << 8));
                
                current_type = 'I';
            
            } else if (operation == TOK_EQEQ || operation == TOK_NOTEQ || operation == TOK_LESS || operation == TOK_LTEQ || operation == TOK_GREATER || operation == TOK_GTEQ) {
            
                unsigned int ifop;
                
                if (operation == TOK_EQEQ) {
                    ifop = 0x32;
                } else if (operation == TOK_NOTEQ) {
                    ifop = 0x33;
                } else if (operation == TOK_LESS) {
                    ifop = 0x34;
                } else if (operation == TOK_GTEQ) {
                    ifop = 0x35;
                } else if (operation == TOK_GREATER) {
                    ifop = 0x36;
                } else {
                    ifop = 0x37;
                }
                
                {
                
                    unsigned long if_left_reg = left_reg;
                    unsigned long if_right_reg = right_reg;
                    unsigned long if_result_reg = tmp_reg;
                    
                    if (if_left_reg >= 16) {
                    
                        dex_u2 (buf, 0x0002);
                        dex_u2 (buf, (unsigned int) if_left_reg);
                        
                        if_left_reg = 0;
                    
                    }
                    
                    if (if_right_reg >= 16) {
                    
                        dex_u2 (buf, 0x0102);
                        dex_u2 (buf, (unsigned int) if_right_reg);
                        
                        if_right_reg = 1;
                    
                    }
                    
                    if (if_result_reg >= 16) {
                        if_result_reg = 2;
                    }
                    
                    dex_u2 (buf, ifop | ((unsigned int) if_left_reg << 8) | ((unsigned int) if_right_reg << 12));
                    dex_u2 (buf, 4);
                    
                    dex_u2 (buf, 0x0012 | ((unsigned int) if_result_reg << 8));
                    dex_u2 (buf, 0x0228);
                    dex_u2 (buf, 0x1012 | ((unsigned int) if_result_reg << 8));
                    
                    if (tmp_reg >= 16) {
                    
                        dex_u2 (buf, 0x0002 | ((unsigned int) tmp_reg << 8));
                        dex_u2 (buf, (unsigned int) if_result_reg);
                    
                    }
                
                }
                
                current_type = 'I';
            
            } else {
            
                unsigned int opcode = dex_binary_opcode_for_type (current_type, operation);
                
                dex_u2 (buf, opcode | ((unsigned int) tmp_reg << 8));
                dex_u2 (buf, (unsigned int) left_reg | ((unsigned int) right_reg << 8));
            
            }
        
        }
        
        free (factor);
        
        if (!end) {
            break;
        }
        
        p = end + 1;
    
    }
    
    if (current_type != field_type[0]) {
        dex_write_numeric_conversion (buf, current_type, field_type[0], tmp_reg, tmp_reg);
    }

}

static void dex_write_mul_di_value (struct dex_buf *buf, unsigned long call_idx, unsigned long dst_reg, const char *factor) {

    const char *operation = 0;
    
    char factor_text[64];
    long adjustment = 0;
    
    unsigned long factor_len;
    operation = strchr (factor, '+');
    
    if (!operation) {
        operation = strchr (factor, '-');
    }
    
    if (operation) {
    
        factor_len = (unsigned long) (operation - factor);
        
        if (factor_len >= sizeof (factor_text)) {
            factor_len = sizeof (factor_text) - 1;
        }
        
        memcpy (factor_text, factor, factor_len);
        factor_text[factor_len] = '\0';
        
        adjustment = strtol (operation + 1, 0, 0);
        
        if (*operation == '-') {
            adjustment = -adjustment;
        }
    
    } else {
        
        strncpy (factor_text, factor, sizeof (factor_text) - 1);
        factor_text[sizeof (factor_text) - 1] = '\0';
    
    }
    
    dex_u2 (buf, 0x0071);
    dex_u2 (buf, call_idx);
    dex_u2 (buf, 0x0000);
    dex_u2 (buf, dex_move_result_opcode ("D", dst_reg));
    
    dex_write_const_integer (buf, "I", dst_reg + 2, factor_text);
    dex_write_int_to_double (buf, dst_reg + 2, dst_reg + 2);
    dex_write_mul_double (buf, dst_reg, dst_reg, dst_reg + 2);
    dex_write_double_to_int (buf, dst_reg, dst_reg);
    
    if (adjustment >= -128 && adjustment <= 127 && adjustment != 0) {
    
        dex_u2 (buf, 0x00d8 | ((unsigned int) dst_reg << 8));
        dex_u2 (buf, ((unsigned int) dst_reg & 0xff) | (((unsigned int) adjustment & 0xff) << 8));
    
    }

}

static int dex_write_instance_field_initializers_single (struct dex_buf *buf, struct source_names *names, struct dex_string_ref *strings, unsigned long nb_strings, unsigned long *field_id_idx, unsigned long *field_type_idx, unsigned long *method_id_idx, struct external_method_info *external_methods, unsigned long nb_external_methods, unsigned long this_reg) {

    unsigned long i;
    
    for (i = 0; i < names->nb_fields; i++) {
    
        if ((names->fields[i].flags & METHOD_STATIC) || !names->fields[i].has_initializer) {
            continue;
        }
        
        if (!field_init_supported (names, names->fields[i].type) && !names->fields[i].is_constructor_initializer) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "field initializer for type '%s' is not supported yet", names->fields[i].type);
            return 1;
        
        }
        
        if (names->fields[i].is_constructor_initializer) {
        
            const char *params;
            unsigned long init_idx;
            
            params = names->fields[i].init_text[5] == 'S' ? declared_reference_call_parameter (names->fields[i].type, "<init>") : "";
            
            if (!params) {
            
                report_field_init_error (&names->fields[i], "constructor parameter type cannot be resolved from source declarations");
                return 1;
            
            }
            
            init_idx = find_external_method_id (external_methods, nb_external_methods, names->fields[i].type, "<init>", "V", params);
            
            if (init_idx == METHOD_NOT_FOUND) {
            
                report_field_init_error (&names->fields[i], "constructor for object field initializer was not found");
                return 1;
            
            }
            
            dex_u2 (buf, 0x0022);
            dex_u2 (buf, field_type_idx[i]);
            
            if (names->fields[i].init_text[5] == 'S') {
            
                dex_u2 (buf, 0x011a);
                
                dex_u2 (buf, dex_find_string (strings, nb_strings, names->fields[i].init_text + 6));
                dex_u2 (buf, 0x2070);
            
            } else {
                dex_u2 (buf, 0x1070);
            }
            
            dex_u2 (buf, init_idx);
            dex_u2 (buf, names->fields[i].init_text[5] == 'S' ? 0x0010 : 0x0000);
            
            dex_u2 (buf, dex_iput_opcode_for_type (names->fields[i].type) | ((unsigned int) this_reg << 12));
            dex_u2 (buf, field_id_idx[i]);
        
        } else if (field_initializer_is_array (names->fields[i].type, names->fields[i].init_text)) {
        
            unsigned long count = field_array_count (names->fields[i].init_text);
            unsigned long k;
            
            const char *p = strchr (names->fields[i].init_text, '|');
            
            dex_u2 (buf, 0x0012 | (1 << 8) | ((unsigned int) count << 12));
            dex_u2 (buf, 0x0023 | (0 << 8) | (1 << 12));
            dex_u2 (buf, field_type_idx[i]);
            
            for (k = 0; k < count; k++) {
            
                char *value = 0;
                unsigned long str_idx;
                
                p = field_array_next (p, &value);
                
                if (!p) {
                    return 1;
                }
                
                str_idx = dex_find_string (strings, nb_strings, value);
                
                dex_u2 (buf, 0x0012 | (1 << 8) | ((unsigned int) k << 12));
                dex_u2 (buf, 0x001a | (2 << 8));
                
                dex_u2 (buf, str_idx);
                dex_u2 (buf, 0x004d | (2 << 8));
                dex_u2 (buf, 0 | (1 << 8));
                
                free (value);
            
            }
            
            dex_u2 (buf, dex_iput_opcode_for_type (names->fields[i].type) | ((unsigned int) this_reg << 12));
            dex_u2 (buf, field_id_idx[i]);
        
        } else if (field_initializer_is_mul_di (names->fields[i].init_text)) {
        
            char *target = 0;
            char *method = 0;
            char *factor = 0;
            
            unsigned long call_idx;
            
            if (split_field_mul_di_initializer (names->fields[i].init_text, &target, &method, &factor)) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "bad multiplied method call field initializer");
                return 1;
            
            }
            
            call_idx = find_static_method_id_single (names, method_id_idx, target, method);
            
            if (call_idx == METHOD_NOT_FOUND) {
            
                char *target_desc = resolve_class_type (names, target);
                call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, method, "D", "");
                
                free (target_desc);
            
            }
            
            if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
            
                report_unknown_static_field_method (&names->fields[i], target, method);
                
                free (target);
                free (method);
                free (factor);
                
                return 1;
            
            }
            
            dex_write_mul_di_value (buf, call_idx, 0, factor);
            dex_u2 (buf, dex_iput_opcode_for_type (names->fields[i].type) | (0 << 8) | ((unsigned int) this_reg << 12));
            dex_u2 (buf, field_id_idx[i]);
            
            free (target);
            free (method);
            free (factor);
        
        } else if (field_initializer_is_call (names->fields[i].init_text)) {
        
            char *target = 0;
            char *method = 0;
            
            unsigned long call_idx;
            
            if (split_field_call_initializer (names->fields[i].init_text, &target, &method)) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "bad method call field initializer");
                return 1;
            
            }
            
            call_idx = find_static_method_id_single_typed (names, method_id_idx, target, method, names->fields[i].type);
            
            if (call_idx == METHOD_NOT_FOUND && find_static_method_id_single (names, method_id_idx, target, method) != METHOD_NOT_FOUND) {
            
                report_field_init_error (&names->fields[i], "incompatible types: possible lossy conversion in field initializer");
                
                free (target);
                free (method);
                
                return 1;
            
            }
            
            if (call_idx == METHOD_NOT_FOUND) {
            
                char *target_desc = resolve_class_type (names, target);
                call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, method, names->fields[i].type, "");
                
                free (target_desc);
            
            }
            
            if (call_idx == METHOD_ACCESS_DENIED) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "private method '%s.%s' is not accessible from '%s'", target, method, names->class_name);
                
                free (target);
                free (method);
                
                return 1;
            
            }
            
            if (call_idx == METHOD_NOT_FOUND) {
            
                report_unknown_static_field_method (&names->fields[i], target, method);
                
                free (target);
                free (method);
                
                return 1;
            
            }
            
            dex_u2 (buf, 0x0071);
            dex_u2 (buf, call_idx);
            dex_u2 (buf, 0x0000);
            dex_u2 (buf, dex_move_result_opcode (names->fields[i].type, 0));
            dex_u2 (buf, dex_iput_opcode_for_type (names->fields[i].type) | (0 << 8) | ((unsigned int) this_reg << 12));
            dex_u2 (buf, field_id_idx[i]);
            
            free (target);
            free (method);
        
        } else if ((names->fields[i].type[0] == 'L')) {
        
            unsigned long str_idx = dex_find_string (strings, nb_strings, names->fields[i].init_text);
            
            dex_u2 (buf, 0x001a);
            dex_u2 (buf, str_idx);
            
            dex_u2 (buf, dex_iput_opcode_for_type (names->fields[i].type) | ((unsigned int) this_reg << 12));
            dex_u2 (buf, field_id_idx[i]);
        
        } else {
        
            dex_write_const_integer (buf, names->fields[i].type, 0, names->fields[i].init_text);
            dex_u2 (buf, dex_iput_opcode_for_type (names->fields[i].type) | (0 << 8) | ((unsigned int) this_reg << 12));
            dex_u2 (buf, field_id_idx[i]);
        
        }
    
    }
    
    return 0;

}

static void free_external_methods (struct external_method_info *methods, unsigned long nb_methods) {

    unsigned long i;
    
    for (i = 0; i < nb_methods; i++) {
    
        free (methods[i].class_desc);
        free (methods[i].name);
        free (methods[i].return_type);
        free (methods[i].params);
        free (methods[i].shorty);
    
    }
    
    free (methods);

}

static int external_method_matches (struct external_method_info *method, const char *class_desc, const char *name, const char *return_type, const char *params) {

    return strcmp (method->class_desc, class_desc) == 0
        && strcmp (method->name, name) == 0
            && strcmp (method->return_type, return_type) == 0
                && strcmp (method->params, params) == 0;

}

static unsigned long add_external_method (struct external_method_info **methods, unsigned long *nb_methods, const char *class_desc, const char *name, const char *return_type, const char *params) {

    struct external_method_info *method;
    unsigned long i;
    
    for (i = 0; i < *nb_methods; i++) {
    
        if (external_method_matches (&(*methods)[i], class_desc, name, return_type, params)) {
            return i;
        }
    
    }
    
    *methods = xrealloc (*methods, (*nb_methods + 1) * sizeof (**methods));
    method = &(*methods)[(*nb_methods)++];
    
    method->class_desc = xstrdup (class_desc);
    method->name = xstrdup (name);
    method->return_type = xstrdup (return_type);
    method->params = xstrdup (params);
    method->shorty = make_method_shorty (return_type, params);
    
    method->class_type_idx = 0;
    method->proto_idx = 0;
    method->name_idx = 0;
    method->method_id_idx = 0;
    method->params_off = 0;
    
    return *nb_methods - 1;

}

static unsigned long find_external_method_id (struct external_method_info *methods, unsigned long nb_methods, const char *class_desc, const char *name, const char *return_type, const char *params) {

    unsigned long i;
    
    for (i = 0; i < nb_methods; i++) {
    
        if (external_method_matches (&methods[i], class_desc, name, return_type, params)) {
            return methods[i].method_id_idx;
        }
    
    }
    
    return 0xFFFFFFFFUL;

}

static int write_type_list_for_params (struct dex_buf *buf, const char **types, unsigned long nb_types, const char *params, unsigned long *off_out) {

    const char *p = params;
    unsigned long count = count_descriptor_params (p);
    
    *off_out = 0;
    
    if (!count) {
        return 0;
    }
    
    *off_out = dex_pos (buf);
    dex_u4 (buf, count);
    
    while (*p) {
    
        const char *start = p;
        char *param_desc;
        unsigned long len, ti;
        
        while (*p == '[') {
            p++;
        }
        
        if (*p == 'L') {
        
            while (*p && *p != ';') {
                p++;
            }
            
            if (*p == ';') {
                p++;
            }
        
        } else if (*p) {
            p++;
        }
        
        len = (unsigned long) (p - start);
        
        param_desc = xmalloc (len + 1);
        memcpy (param_desc, start, len);
        
        param_desc[len] = '\0';
        
        ti = find_type_idx (types, nb_types, param_desc);
        dex_u2 (buf, ti);
        
        free (param_desc);
    
    }
    
    dex_align4 (buf);
    return 0;

}

static int target_is_single_source_class (struct source_names *names, const char *target) {
    return strcmp (target, names->class_name) == 0;
}

static int target_is_multi_source_class (struct source_names *all_names, unsigned long nb_sources, const char *target) {

    unsigned long i;
    
    for (i = 0; i < nb_sources; i++) {
    
        if (strcmp (all_names[i].class_name, target) == 0) {
            return 1;
        }
    
    }
    
    return 0;

}

static char *descriptor_to_simple_class_name (const char *desc);
static int statement_is_field_string_call (struct source_names *names, struct statement_info *stmt);

struct multi_method_map {

    unsigned long source_index;
    unsigned long method_index;

};

struct multi_field_map {

    unsigned long source_index;
    unsigned long field_index;

};

static unsigned long find_method_id_multi_by_class_name_params (struct source_names *all_names, struct multi_method_map *method_map, unsigned long nb_real_methods, unsigned long *method_id_idx, unsigned long caller_source_index, const char *class_name, const char *name, const char *params) {

    unsigned long i;
    
    for (i = 0; i < nb_real_methods; i++) {
    
        struct source_names *names = &all_names[method_map[i].source_index];
        struct method_info *method = &names->methods[method_map[i].method_index];
        
        if (strcmp (names->class_name, class_name) == 0 && strcmp (method->java_name, name) == 0 && strcmp (method->params, params) == 0) {
        
            if ((method->flags & METHOD_PRIVATE) && method_map[i].source_index != caller_source_index) {
                return METHOD_ACCESS_DENIED;
            }
            
            return method_id_idx[i];
        
        }
    
    }
    
    return METHOD_NOT_FOUND;

}

static unsigned long find_method_id_multi_by_class_name (struct source_names *all_names, struct multi_method_map *method_map, unsigned long nb_real_methods, unsigned long *method_id_idx, unsigned long caller_source_index, const char *class_name, const char *name);
static unsigned long find_method_id_multi_by_class_name_typed (struct source_names *all_names, struct multi_method_map *method_map, unsigned long nb_real_methods, unsigned long *method_id_idx, unsigned long caller_source_index, const char *class_name, const char *name, const char *return_type);

static int dex_write_instance_field_initializers_multi (struct dex_buf *buf, struct source_names *all_names, struct multi_field_map *field_map, struct multi_method_map *method_map, unsigned long nb_real_fields, unsigned long nb_real_methods, struct dex_string_ref *strings, unsigned long nb_strings, unsigned long *field_id_idx, unsigned long *field_type_idx, unsigned long *method_id_idx, struct external_method_info *external_methods, unsigned long nb_external_methods, unsigned long source_index, unsigned long this_reg) {

    struct source_names *names = &all_names[source_index];
    unsigned long j, m;
    
    for (j = 0; j < names->nb_fields; j++) {
    
        unsigned long global_field = 0;
        
        if ((names->fields[j].flags & METHOD_STATIC) || !names->fields[j].has_initializer) {
            continue;
        }
        
        if (!field_init_supported (names, names->fields[j].type) && !names->fields[j].is_constructor_initializer) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "field initializer for type '%s' is not supported yet", names->fields[j].type);
            return 1;
        
        }
        
        for (m = 0; m < nb_real_fields; m++) {
        
            if (field_map[m].source_index == source_index && field_map[m].field_index == j) {
            
                global_field = m;
                break;
            
            }
        
        }
        
        if (names->fields[j].is_constructor_initializer) {
        
            const char *params;
            unsigned long init_idx;
            
            params = names->fields[j].init_text[5] == 'S' ? declared_reference_call_parameter (names->fields[j].type, "<init>") : "";
            
            if (!params) {
            
                report_field_init_error (&names->fields[j], "constructor parameter type cannot be resolved from source declarations");
                return 1;
            
            }
            
            char *class_name = descriptor_to_simple_class_name (names->fields[j].type);
            init_idx = find_method_id_multi_by_class_name_params (all_names, method_map, nb_real_methods, method_id_idx, source_index, class_name, "<init>", params);
            
            if (init_idx == METHOD_NOT_FOUND) {
                init_idx = find_external_method_id (external_methods, nb_external_methods, names->fields[j].type, "<init>", "V", params);
            }
            
            free (class_name);
            
            if (init_idx == METHOD_ACCESS_DENIED || init_idx == METHOD_NOT_FOUND) {
            
                report_field_init_error (&names->fields[j], "constructor for object field initializer is missing or inaccessible");
                return 1;
            
            }
            dex_u2 (buf, 0x0022);
            dex_u2 (buf, field_type_idx[global_field]);
            
            if (names->fields[j].init_text[5] == 'S') {
            
                dex_u2 (buf, 0x011a);
                
                dex_u2 (buf, dex_find_string (strings, nb_strings, names->fields[j].init_text + 6));
                dex_u2 (buf, 0x2070);
            
            } else {
                dex_u2 (buf, 0x1070);
            }
            
            dex_u2 (buf, init_idx);
            dex_u2 (buf, names->fields[j].init_text[5] == 'S' ? 0x0010 : 0x0000);
            
            dex_u2 (buf, dex_iput_opcode_for_type (names->fields[j].type) | ((unsigned int) this_reg << 12));
            dex_u2 (buf, field_id_idx[global_field]);
        
        } else if (field_initializer_is_array (names->fields[j].type, names->fields[j].init_text)) {
        
            unsigned long count = field_array_count (names->fields[j].init_text);
            unsigned long k;
            
            const char *p = strchr (names->fields[j].init_text, '|');
            
            dex_u2 (buf, 0x0012 | (1 << 8) | ((unsigned int) count << 12));
            dex_u2 (buf, 0x0023 | (0 << 8) | (1 << 12));
            dex_u2 (buf, field_type_idx[global_field]);
            
            for (k = 0; k < count; k++) {
            
                char *value = 0;
                unsigned long str_idx;
                
                p = field_array_next (p, &value);
                
                if (!p) {
                    return 1;
                }
                
                str_idx = dex_find_string (strings, nb_strings, value);
                
                dex_u2 (buf, 0x0012 | (1 << 8) | ((unsigned int) k << 12));
                dex_u2 (buf, 0x001a | (2 << 8));
                
                dex_u2 (buf, str_idx);
                dex_u2 (buf, 0x004d | (2 << 8));
                dex_u2 (buf, 0 | (1 << 8));
                
                free (value);
            
            }
            
            dex_u2 (buf, dex_iput_opcode_for_type (names->fields[j].type) | ((unsigned int) this_reg << 12));
            dex_u2 (buf, field_id_idx[global_field]);
        
        } else if (field_initializer_is_mul_di (names->fields[j].init_text)) {
        
            char *target = 0;
            char *method = 0;
            char *factor = 0;
            
            unsigned long call_idx;
            
            if (split_field_mul_di_initializer (names->fields[j].init_text, &target, &method, &factor)) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "bad multiplied method call field initializer");
                return 1;
            
            }
            
            call_idx = find_method_id_multi_by_class_name_typed (all_names, method_map, nb_real_methods, method_id_idx, source_index, target, method, names->fields[j].type);
            
            if (call_idx == METHOD_NOT_FOUND && find_method_id_multi_by_class_name (all_names, method_map, nb_real_methods, method_id_idx, source_index, target, method) != METHOD_NOT_FOUND) {
            
                report_field_init_error (&names->fields[j], "incompatible types: possible lossy conversion in field initializer");
                
                free (target);
                free (method);
                
                return 1;
            
            }
            
            if (call_idx == METHOD_NOT_FOUND) {
            
                char *target_desc = resolve_class_type (names, target);
                call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, method, "D", "");
                
                free (target_desc);
            
            }
            
            if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
            
                report_unknown_static_field_method (&names->fields[j], target, method);
                
                free (target);
                free (method);
                free (factor);
                
                return 1;
            
            }
            
            dex_write_mul_di_value (buf, call_idx, 0, factor);
            dex_u2 (buf, dex_iput_opcode_for_type (names->fields[j].type) | (0 << 8) | ((unsigned int) this_reg << 12));
            dex_u2 (buf, field_id_idx[global_field]);
            
            free (target);
            free (method);
            free (factor);
        
        } else if (field_initializer_is_call (names->fields[j].init_text)) {
        
            char *target = 0;
            char *method = 0;
            
            unsigned long call_idx;
            
            if (split_field_call_initializer (names->fields[j].init_text, &target, &method)) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "bad method call field initializer");
                return 1;
            
            }
            
            call_idx = find_method_id_multi_by_class_name_typed (all_names, method_map, nb_real_methods, method_id_idx, source_index, target, method, names->fields[j].type);
            
            if (call_idx == METHOD_NOT_FOUND && find_method_id_multi_by_class_name (all_names, method_map, nb_real_methods, method_id_idx, source_index, target, method) != METHOD_NOT_FOUND) {
            
                report_field_init_error (&names->fields[j], "incompatible types: possible lossy conversion in field initializer");
                
                free (target);
                free (method);
                
                return 1;
            
            }
            
            if (call_idx == METHOD_NOT_FOUND) {
            
                char *target_desc = resolve_class_type (names, target);
                call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, method, names->fields[j].type, "");
                
                free (target_desc);
            
            }
            
            if (call_idx == METHOD_ACCESS_DENIED) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "private method '%s.%s' is not accessible from '%s'", target, method, names->class_name);
                
                free (target);
                free (method);
                
                return 1;
            
            }
            
            if (call_idx == METHOD_NOT_FOUND) {
            
                report_unknown_static_field_method (&names->fields[j], target, method);
                
                free (target);
                free (method);
                
                return 1;
            
            }
            
            dex_u2 (buf, 0x0071);
            dex_u2 (buf, call_idx);
            dex_u2 (buf, 0x0000);
            dex_u2 (buf, dex_move_result_opcode (names->fields[j].type, 0));
            dex_u2 (buf, dex_iput_opcode_for_type (names->fields[j].type) | (0 << 8) | ((unsigned int) this_reg << 12));
            dex_u2 (buf, field_id_idx[global_field]);
            
            free (target);
            free (method);
        
        } else if ((names->fields[j].type[0] == 'L')) {
        
            unsigned long str_idx = dex_find_string (strings, nb_strings, names->fields[j].init_text);
            
            dex_u2 (buf, 0x001a);
            dex_u2 (buf, str_idx);
            
            dex_u2 (buf, dex_iput_opcode_for_type (names->fields[j].type) | ((unsigned int) this_reg << 12));
            dex_u2 (buf, field_id_idx[global_field]);
        
        } else {
        
            dex_write_const_integer (buf, names->fields[j].type, 0, names->fields[j].init_text);
            dex_u2 (buf, dex_iput_opcode_for_type (names->fields[j].type) | (0 << 8) | ((unsigned int) this_reg << 12));
            dex_u2 (buf, field_id_idx[global_field]);
        
        }
    
    }
    
    return 0;

}

static unsigned long find_field_id_single (struct source_names *names, unsigned long *field_id_idx, const char *field_name);

static int method_has_explicit_constructor_call (struct method_info *method) {

    unsigned long i;
    
    for (i = 0; i < method->nb_statements; i++) {
    
        struct statement_info *stmt = &method->statements[i];
        
        if (stmt->kind == STMT_CALL && stmt->target
            && (strcmp (stmt->target, "@super") == 0 || strcmp (stmt->target, "@this") == 0)
                && stmt->method && strcmp (stmt->method, "<init>") == 0) {
            return 1;
        }
    
    }
    
    return 0;

}

static int semantic_check_method_calls (struct source_names *names, struct method_info *method) {

    unsigned long i, j;
    int ret = 0;
    
    for (i = 0; i < method->nb_statements; i++) {
    
        struct statement_info *stmt = &method->statements[i];
        
        if (stmt->arg_kind == ARG_STRING_CONCAT) {
        
            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret,
                "runtime string concatenation is not implemented");
            
            return 1;
        
        }
        
        if (stmt->kind != STMT_CALL) {
            continue;
        }
        
        if ((method->flags & METHOD_STATIC) && !stmt->target) {
        
            struct method_info *callee = find_method_info_by_java_name (names, stmt->method);
            
            if (callee && !(callee->flags & METHOD_STATIC)) {
            
                report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "non-static method '%s' cannot be referenced from a static method", stmt->method);
                ret = 1;
            
            }
        
        }
        
        if (method->flags & METHOD_STATIC) {
        
            for (j = 0; j < stmt->nb_concat_refs; j++) {
            
                struct field_info *field = find_field_info_by_name (names, stmt->concat_refs[j].name);
                
                if (field && !(field->flags & METHOD_STATIC)) {
                
                    report_line_at (stmt->concat_refs[j].filename, stmt->concat_refs[j].line, REPORT_ERROR, stmt->concat_refs[j].start, stmt->concat_refs[j].caret, "non-static field '%s' cannot be referenced from a static method", field->name);
                    ret = 1;
                
                }
            
            }
        
        }
    
    }
    
    return ret;

}

static int semantic_check_source (struct source_names *names) {

    unsigned long i;
    
    for (i = 0; i < names->nb_methods; i++) {
    
        if (semantic_check_method_calls (names, &names->methods[i])) {
            return 1;
        }
    
    }
    
    return 0;

}

static struct method_info *find_multi_source_method_info (struct source_names *all_names, unsigned long nb_sources, const char *class_name, const char *method_name) {

    const char *wanted_class = simple_name (class_name);
    unsigned long i, j;
    
    for (i = 0; i < nb_sources; i++) {
    
        if (strcmp (all_names[i].class_name, wanted_class) != 0) {
            continue;
        }
        
        for (j = 0; j < all_names[i].nb_methods; j++) {
        
            if (strcmp (all_names[i].methods[j].java_name, method_name) == 0 && all_names[i].methods[j].params[0] == '\0') {
                return &all_names[i].methods[j];
            }
        
        }
    
    }
    
    return 0;

}

static int source_has_qualified_name (struct source_names *names, const char *qualified_name) {

    unsigned long package_len;
    
    if (!names->package_name || !*names->package_name) {
        return strcmp (names->class_name, qualified_name) == 0;
    }
    
    package_len = strlen (names->package_name);
    
    return strncmp (qualified_name, names->package_name, package_len) == 0
        && qualified_name[package_len] == '.'
            && strcmp (qualified_name + package_len + 1, names->class_name) == 0;

}

static int source_is_in_package (struct source_names *names, const char *package_name) {

    if (!names->package_name) {
        return !*package_name;
    }
    
    return strcmp (names->package_name, package_name) == 0;

}

static void report_import_error (struct import_info *imp, const char *message) {

    const char *caret = imp->source_line ? imp->source_line + imp->caret_offset : 0;
    report_line_at (imp->filename, (unsigned int) imp->line, REPORT_ERROR, imp->source_line, caret, "%s", message);

}

static int validate_source_imports (struct source_names *all_names, unsigned long nb_sources, struct source_names *names) {

    unsigned long i, j, k;
    
    for (i = 0; i < names->nb_imports; i++) {
    
        struct import_info *imp = &names->imports[i];
        int found = 0;
        
        if (imp->implicit) {
            continue;
        }
        
        for (j = 0; j < nb_sources; j++) {
        
            if (imp->wildcard) {
                found = source_is_in_package (&all_names[j], imp->name);
            } else {
               found = source_has_qualified_name (&all_names[j], imp->name);
            }
            
            if (found) {
                break;
            }
        
        }
        
        if (!found) {
        
            char *message;
            unsigned long len = strlen (imp->name) + 64;
            
            message = xmalloc (len);
            
            if (imp->wildcard) {
                sprintf (message, "imported package '%s' does not exist", imp->name);
            } else {
                sprintf (message, "imported class '%s' does not exist", imp->name);
            }
            
            report_import_error (imp, message);
            free (message);
            
            return 1;
        
        }
        
        if (imp->wildcard) {
            continue;
        }
        
        for (k = i + 1; k < names->nb_imports; k++) {
        
            struct import_info *other = &names->imports[k];
            
            if (other->implicit || other->wildcard || strcmp (imp->name, other->name) == 0) {
                continue;
            }
            
            if (strcmp (simple_name (imp->name), simple_name (other->name)) == 0) {
            
                char *message;
                unsigned long len = strlen (other->name) + 80;
                
                message = xmalloc (len);
                sprintf (message, "class name '%s' is already imported from another package", simple_name (other->name));
                
                report_import_error (other, message);
                free (message);
                
                return 1;
            
            }
        
        }
    
    }
    
    return 0;

}

static int semantic_check_sources (struct source_names *all_names, unsigned long nb_sources) {

    unsigned long i, j;
    
    for (i = 0; i < nb_sources; i++) {
    
        if (validate_source_imports (all_names, nb_sources, &all_names[i])) {
            return 1;
        }
        
        if (semantic_check_source (&all_names[i])) {
            return 1;
        }
        
        for (j = 0; j < all_names[i].nb_fields; j++) {
        
            struct field_info *field = &all_names[i].fields[j];
            
            if (field_initializer_is_call (field->init_text)) {
            
                struct method_info *callee;
                
                char *target = 0;
                char *method = 0;
                
                if (split_field_call_initializer (field->init_text, &target, &method)) {
                    continue;
                }
                
                callee = find_multi_source_method_info (all_names, nb_sources, target, method);
                
                if (callee && strcmp (callee->return_type, field->type) != 0) {
                
                    report_field_init_error (field, "incompatible types: possible lossy conversion in field initializer");
                    
                    free (target);
                    free (method);
                    
                    return 1;
                
                }
                
                free (target);
                free (method);
            
            }
        
        }
    
    }
    
    return 0;

}

static int write_dex_file (FILE *fp, struct source_names *names) {

    struct dex_buf buf;
    struct dex_string_ref *strings = 0;
    struct external_method_info *external_methods = 0;
    
    char *class_desc;
    char *super_desc = 0;
    
    unsigned long nb_strings = 0;
    unsigned long nb_external_methods = 0;
    
    unsigned long *string_offsets;
    unsigned long *type_string_idx;
    
    const char **types = 0;
    unsigned long nb_types = 0;
    
    unsigned long class_type_idx, super_type_idx;
    unsigned long interface_type_list_off = 0;
    
    unsigned long *method_proto_idx;
    unsigned long *method_name_idx;
    unsigned long *method_id_idx;
    unsigned long *method_params_off;
    unsigned long *method_code_off;
    unsigned long *field_name_idx;
    unsigned long *field_type_idx;
    unsigned long *field_id_idx;
    
    unsigned long string_ids_off, type_ids_off, proto_ids_off, field_ids_off, method_ids_off, class_defs_off, data_off;
    unsigned long i, j;
    unsigned long header_size = 0x70;
    unsigned long class_data_off, static_values_off, map_off;
    unsigned long nb_type_lists = 0, type_lists_off = 0;
    
    unsigned long nb_fields = 0;
    unsigned long nb_static_fields = 0;
    unsigned long nb_instance_fields = 0;
    unsigned long nb_method_ids;
    unsigned long nb_protos;
    unsigned long nb_code_items = 0, code_items_off = 0;
    
    memset (&buf, 0, sizeof (buf));
    
    class_desc = dex_class_descriptor (names);
    super_desc = names->super_class;
    
    for (i = 0; i < names->nb_fields; i++) {
    
        dex_add_string (&strings, &nb_strings, names->fields[i].name);
        dex_add_string (&strings, &nb_strings, names->fields[i].type);
        
        if (names->fields[i].has_initializer && names->fields[i].is_constructor_initializer) {
        
            if (names->fields[i].init_text[5] == 'S') {
                dex_add_string (&strings, &nb_strings, names->fields[i].init_text + 6);
            }
            
            {
            
                const char *params = names->fields[i].init_text[5] == 'S' ? declared_reference_call_parameter (names->fields[i].type, "<init>") : "";
                
                if (!params) {
                
                    report_field_init_error (&names->fields[i], "constructor parameter type cannot be resolved from source declarations");
                    return 1;
                
                }
                
                add_external_method (&external_methods, &nb_external_methods, names->fields[i].type, "<init>", "V", params);
            
            }
        
        } else if (names->fields[i].has_initializer && (names->fields[i].type[0] == 'L')) {
            dex_add_string (&strings, &nb_strings, names->fields[i].init_text);
        } else if (field_initializer_is_array (names->fields[i].type, names->fields[i].init_text)) {
        
            unsigned long count = field_array_count (names->fields[i].init_text);
            unsigned long k;
            
            const char *p = strchr (names->fields[i].init_text, '|');
            
            for (k = 0; k < count; k++) {
            
                char *value = 0;
                
                p = field_array_next (p, &value);
                
                if (!p) {
                    return 1;
                }
                
                dex_add_string (&strings, &nb_strings, value);
                free (value);
            
            }
        
        }
        
        if (names->fields[i].has_initializer && field_initializer_is_mul_di (names->fields[i].init_text)) {
        
            char *target = 0;
            char *method = 0;
            char *factor = 0;
            
            if (split_field_mul_di_initializer (names->fields[i].init_text, &target, &method, &factor)) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "bad multiplied method call field initializer");
                return 1;
            
            }
            
            if (!target_is_single_source_class (names, target)) {
            
                char *target_desc = resolve_class_type (names, target);
                add_external_method (&external_methods, &nb_external_methods, target_desc, method, "D", "");
                
                free (target_desc);
            
            }
            
            free (target);
            free (method);
            free (factor);
        
        } else if (names->fields[i].has_initializer && field_initializer_is_call (names->fields[i].init_text)) {
        
            char *target = 0;
            char *method = 0;
            
            if (split_field_call_initializer (names->fields[i].init_text, &target, &method)) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "bad method call field initializer");
                return 1;
            
            }
            
            if (!target_is_single_source_class (names, target)) {
            
                char *target_desc = resolve_class_type (names, target);
                add_external_method (&external_methods, &nb_external_methods, target_desc, method, names->fields[i].type, "");
                
                free (target_desc);
            
            }
            
            free (target);
            free (method);
        
        }
    
    }
    
    for (i = 0; i < names->nb_methods; i++) {
    
        for (j = 0; j < names->methods[i].nb_statements; j++) {
        
            struct statement_info *stmt = &names->methods[i].statements[j];
            
            if (stmt->kind == STMT_LOCAL && stmt->arg_kind == ARG_NEW) {
            
                const char *local_type = names->methods[i].locals[strtoul (stmt->target + 1, 0, 10)].type;
                
                char *ctor_params = 0;
                unsigned long *ctor_locals = 0, nb_ctor_locals = 0;
                
                if (split_constructor_arguments (stmt->arg_text, &ctor_params, &ctor_locals, &nb_ctor_locals)) {
                    return 1;
                }
                
                add_external_method (&external_methods, &nb_external_methods, local_type, "<init>", "V", ctor_params);
                
                free (ctor_params);
                free (ctor_locals);
            
            }
            
            if (statement_is_field_string_call (names, stmt)) {
                continue;
            } else if (stmt->kind == STMT_CALL && stmt->target && stmt->target[0] == '$') {
            
                unsigned long local_index = strtoul (stmt->target + 1, 0, 10);
                const char *target_desc = names->methods[i].locals[local_index].type;
                
                const char *return_type = "V";
                char *class_desc;
                
                const char *params = "";
                char *call_params = 0;
                
                unsigned long *call_locals = 0, nb_call_locals = 0;
                
                if (stmt->arg_kind == ARG_RESULT_CALL && stmt->arg_text && stmt->arg_text[0] == '$') {
                    return_type = names->methods[i].locals[strtoul (stmt->arg_text + 1, 0, 10)].type;
                } else if (stmt->arg_kind == ARG_RESULT_MULTI) {
                
                    return_type = names->methods[i].locals[stmt->arg_param_index].type;
                    
                    if (split_constructor_arguments (stmt->arg_text, &call_params, &call_locals, &nb_call_locals)) {
                        return 1;
                    }
                    
                    params = call_params;
                
                }
                
                class_desc = dex_class_descriptor (names);
                
                if (strcmp (target_desc, class_desc) != 0) {
                    add_external_method (&external_methods, &nb_external_methods, target_desc, stmt->method, return_type, params);
                }
                
                free (call_params);
                free (call_locals);
                
                free (class_desc);
            
            } else if (stmt->kind == STMT_CALL && stmt->target && !target_is_single_source_class (names, stmt->target)) {
            
                const char *params = "";
                const char *return_type = "V";
                
                char *call_params = 0;
                unsigned long *call_locals = 0, nb_call_locals = 0;
                
                char *target_desc = resolve_class_type (names, stmt->target);
                
                if (stmt->arg_kind == ARG_STRING || stmt->arg_kind == ARG_STRING_CONCAT) {
                
                    params = declared_reference_call_parameter (target_desc, stmt->method);
                    
                    if (!params) {
                    
                        report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret,
                            "method parameter type cannot be resolved from source declarations");
                        
                        free (target_desc);
                        return 1;
                    
                    }
                
                }
                
                if (stmt->arg_kind == ARG_RESULT_MULTI || stmt->arg_kind == ARG_MULTI) {
                
                    if (split_constructor_arguments (stmt->arg_text, &call_params, &call_locals, &nb_call_locals)) {
                    
                        free (target_desc);
                        return 1;
                    
                    }
                    
                    params = call_params;
                
                }
                
                if (stmt->arg_kind == ARG_RESULT_MULTI) {
                    return_type = names->methods[i].locals[stmt->arg_param_index].type;
                }
                
                add_external_method (&external_methods, &nb_external_methods, target_desc, stmt->method, return_type, params);
                
                free (call_params);
                free (call_locals);
                free (target_desc);
            
            } else if (stmt->kind == STMT_RETURN && stmt->arg_kind == ARG_CALL && stmt->target && !target_is_single_source_class (names, stmt->target)) {
            
                char *target_desc = resolve_class_type (names, stmt->target);
                
                add_external_method (&external_methods, &nb_external_methods, target_desc, stmt->method, names->methods[i].return_type, "");
                free (target_desc);
            
            }
        
        }
    
    }
    
    dex_add_string (&strings, &nb_strings, class_desc);
    
    if (super_desc) {
        dex_add_string (&strings, &nb_strings, super_desc);
    }
    
    for (i = 0; i < names->nb_interfaces; i++) {
        dex_add_string (&strings, &nb_strings, names->interfaces[i]);
    }
    
    dex_add_string (&strings, &nb_strings, "<init>");
    dex_add_string (&strings, &nb_strings, "V");
    
    for (i = 0; i < names->nb_methods; i++) {
    
        const char *p;
        
        dex_add_string (&strings, &nb_strings, names->methods[i].name);
        dex_add_string (&strings, &nb_strings, names->methods[i].return_type);
        dex_add_string (&strings, &nb_strings, names->methods[i].shorty);
        
        for (j = 0; j < names->methods[i].nb_statements; j++) {

            if ((names->methods[i].statements[j].kind == STMT_CALL || names->methods[i].statements[j].kind == STMT_RETURN || names->methods[i].statements[j].kind == STMT_LOCAL) && names->methods[i].statements[j].arg_kind == ARG_STRING) {
                dex_add_string (&strings, &nb_strings, names->methods[i].statements[j].arg_text);
            } else if (names->methods[i].statements[j].kind == STMT_CALL && names->methods[i].statements[j].arg_kind == ARG_STRING_CONCAT) {
            
                const char *cp = names->methods[i].statements[j].arg_text;
                
                while (cp && *cp) {
                
                    const char *cq = strchr (cp, '\037');
                    
                    if (!cq) {
                        cq = cp + strlen (cp);
                    }
                    
                    if (cq > cp && cp[0] == 'S' && cp[1] == ':') {
                    
                        unsigned long clen = (unsigned long) (cq - (cp + 2));
                        char *ctmp = xmalloc (clen + 1);
                        
                        memcpy (ctmp, cp + 2, clen);
                        ctmp[clen] = '\0';
                        
                        dex_add_string (&strings, &nb_strings, ctmp);
                    
                    }
                    
                    cp = (*cq == '\037') ? cq + 1 : cq;
                
                }
            
            }
        
        }
        
        for (p = names->methods[i].params; *p; ) {
        
            const char *start = p;
            
            char *param_desc;
            unsigned long len;
            
            while (*p == '[') {
                p++;
            }
            
            if (*p == 'L') {
            
                while (*p && *p != ';') {
                    p++;
                }
                
                if (*p == ';') {
                    p++;
                }
            
            } else if (*p) {
                p++;
            }
            
            len = (unsigned long) (p - start);
            
            param_desc = xmalloc (len + 1);
            memcpy (param_desc, start, len);
            
            param_desc[len] = '\0';
            dex_add_string (&strings, &nb_strings, param_desc);
        
        }
    
    }
    
    for (i = 0; i < nb_external_methods; i++) {
    
        const char *p;
        
        dex_add_string (&strings, &nb_strings, external_methods[i].class_desc);
        dex_add_string (&strings, &nb_strings, external_methods[i].name);
        dex_add_string (&strings, &nb_strings, external_methods[i].return_type);
        dex_add_string (&strings, &nb_strings, external_methods[i].shorty);
        
        for (p = external_methods[i].params; *p; ) {
        
            const char *start = p;
            char *param_desc;
            unsigned long len;
            
            while (*p == '[') {
                p++;
            }
            
            if (*p == 'L') {
            
                while (*p && *p != ';') {
                    p++;
                }
                
                if (*p == ';') {
                    p++;
                }
            
            } else if (*p) {
                p++;
            }
            
            len = (unsigned long) (p - start);
            
            param_desc = xmalloc (len + 1);
            memcpy (param_desc, start, len);
            
            param_desc[len] = '\0';
            
            dex_add_string (&strings, &nb_strings, param_desc);
            free (param_desc);
        
        }
    
    }
    
    qsort (strings, nb_strings, sizeof (*strings), dex_string_cmp);
    
    for (i = 0; i < nb_strings; i++) {
        strings[i].index = i;
    }
    
    for (i = 0; i < nb_strings; i++) {
    
        if (strings[i].text[0] == 'L' || strings[i].text[0] == '[' || strlen (strings[i].text) == 1) {
        
            types = xrealloc (types, (nb_types + 1) * sizeof (*types));
            types[nb_types++] = strings[i].text;
        
        }
    
    }
    
    for (i = 0; i < nb_types; i++) {
    
        for (j = i + 1; j < nb_types; j++) {
        
            if (dex_type_order (types[j], types[i], strings, nb_strings) < 0) {
                const char *tmp = types[i]; types[i] = types[j]; types[j] = tmp;
            }
        
        }
    
    }
    
    class_type_idx = find_type_idx (types, nb_types, class_desc);
    super_type_idx = super_desc ? find_type_idx (types, nb_types, super_desc) : 0xFFFFFFFFUL;
    
    nb_fields = names->nb_fields;

    for (i = 0; i < names->nb_fields; i++) {
    
        if (names->fields[i].flags & METHOD_STATIC) {
            nb_static_fields++;
        } else {
            nb_instance_fields++;
        }
    
    }
    
    nb_method_ids = names->nb_methods + nb_external_methods;
    nb_protos = names->nb_methods + nb_external_methods;
    
    method_proto_idx = xmalloc (names->nb_methods * sizeof (*method_proto_idx));
    method_name_idx = xmalloc (names->nb_methods * sizeof (*method_name_idx));
    method_id_idx = xmalloc (names->nb_methods * sizeof (*method_id_idx));
    method_params_off = xmalloc (names->nb_methods * sizeof (*method_params_off));
    method_code_off = xmalloc (names->nb_methods * sizeof (*method_code_off));
    
    field_name_idx = xmalloc (names->nb_fields * sizeof (*field_name_idx));
    field_type_idx = xmalloc (names->nb_fields * sizeof (*field_type_idx));
    field_id_idx = xmalloc (names->nb_fields * sizeof (*field_id_idx));
    
    for (i = 0; i < names->nb_fields; i++) {
    
        field_name_idx[i] = dex_find_string (strings, nb_strings, names->fields[i].name);
        field_type_idx[i] = find_type_idx (types, nb_types, names->fields[i].type);
        
        field_id_idx[i] = i;
    
    }
    
    for (i = 0; i < names->nb_methods; i++) {
    
        method_proto_idx[i] = i;
        
        method_name_idx[i] = dex_find_string (strings, nb_strings, names->methods[i].name);
        method_id_idx[i] = i;
        
        method_params_off[i] = 0;
        method_code_off[i] = 0;
    
    }
    
    for (i = 0; i < nb_external_methods; i++) {
    
        external_methods[i].proto_idx = names->nb_methods + i;
        external_methods[i].name_idx = dex_find_string (strings, nb_strings, external_methods[i].name);
        external_methods[i].method_id_idx = names->nb_methods + i;
        external_methods[i].class_type_idx = find_type_idx (types, nb_types, external_methods[i].class_desc);
        external_methods[i].params_off = 0;
    
    }
    
    string_ids_off = header_size;
    type_ids_off = string_ids_off + nb_strings * 4;
    proto_ids_off = type_ids_off + nb_types * 4;
    field_ids_off = proto_ids_off + nb_protos * 12;
    method_ids_off = field_ids_off + nb_fields * 8;
    class_defs_off = method_ids_off + nb_method_ids * 8;
    data_off = class_defs_off + 32;
    
    while (buf.size < data_off) {
        dex_u1 (&buf, 0);
    }
    
    string_offsets = xmalloc (nb_strings * sizeof (*string_offsets));
    
    for (i = 0; i < nb_strings; i++) {
    
        string_offsets[i] = dex_pos (&buf);
        
        dex_uleb128 (&buf, strlen (strings[i].text));
        dex_bytes (&buf, strings[i].text, strlen (strings[i].text));
        
        dex_u1 (&buf, 0);
    
    }
    
    dex_align4 (&buf);
    
    for (i = 0; i < names->nb_methods; i++) {
    
        const char *p = names->methods[i].params;
        unsigned long count = count_descriptor_params (p);
        
        if (count) {
        
            if (!type_lists_off) {
                type_lists_off = dex_pos (&buf);
            }
            
            nb_type_lists++;
            
            method_params_off[i] = dex_pos (&buf);
            dex_u4 (&buf, count);
            
            while (*p) {
            
                const char *start = p;
                
                char *param_desc;
                unsigned long len, ti;
                
                while (*p == '[') {
                    p++;
                }
                
                if (*p == 'L') {
                
                    while (*p && *p != ';') {
                        p++;
                    }
                    
                    if (*p == ';') {
                        p++;
                    }
                
                } else if (*p) {
                    p++;
                }
                
                len = (unsigned long) (p - start);
                
                param_desc = xmalloc (len + 1);
                memcpy (param_desc, start, len);
                
                param_desc[len] = '\0';
                
                ti = find_type_idx (types, nb_types, param_desc);
                dex_u2 (&buf, ti);
                
                free (param_desc);
            
            }
            
            dex_align4 (&buf);
        
        }
    
    }
    
    for (i = 0; i < nb_external_methods; i++) {
    
        if (count_descriptor_params (external_methods[i].params)) {
        
            if (!type_lists_off) {
                type_lists_off = dex_pos (&buf);
            }
            
            nb_type_lists++;
            write_type_list_for_params (&buf, types, nb_types, external_methods[i].params, &external_methods[i].params_off);
        
        }
    
    }
    
    if (names->nb_interfaces) {
    
        if (!type_lists_off) {
            type_lists_off = dex_pos (&buf);
        }
        
        nb_type_lists++;
        
        interface_type_list_off = dex_pos (&buf);
        dex_u4 (&buf, names->nb_interfaces);
        
        for (i = 0; i < names->nb_interfaces; i++) {
            dex_u2 (&buf, find_type_idx (types, nb_types, names->interfaces[i]));
        }
        
        dex_align4 (&buf);
    
    }
    
    for (i = 0; i < names->nb_methods; i++) {
    
        unsigned long ins_size = count_descriptor_registers (names->methods[i].params);
        unsigned long registers_size;
        unsigned long insns_size;
        
        if (!(names->methods[i].flags & METHOD_STATIC)) {
            ins_size++;
        }
        
        if (names->methods[i].flags & (METHOD_NATIVE | METHOD_ABSTRACT)) {
            continue;
        }
        
        dex_align4 (&buf);
        
        if (!code_items_off) {
            code_items_off = dex_pos (&buf);
        }
        
        method_code_off[i] = dex_pos (&buf);
        nb_code_items++;
        
        if (strcmp (names->methods[i].name, "<init>") == 0) {
        
            unsigned long nb_field_init_units = count_instance_field_initializer_code_units (names);
            unsigned long this_reg;
            
            registers_size = (ins_size ? ins_size : 1) + (nb_field_init_units ? 4 : 0);
            this_reg = registers_size - (ins_size ? ins_size : 1);
            
            dex_write_code_item_header (&buf, registers_size, ins_size ? ins_size : 1, 1, 1 + nb_field_init_units);
            
            if (dex_write_instance_field_initializers_single (&buf, names, strings, nb_strings, field_id_idx, field_type_idx, method_id_idx, external_methods, nb_external_methods, this_reg)) {
                return 1;
            }
            
            dex_u2 (&buf, 0x000e);
        
        } else {
        
            unsigned long insns_start_pos, si;
            int has_return = 0;
            
            registers_size = ins_size + 6 + method_local_slot_count (&names->methods[i]);
            insns_size = 0;
            
            for (si = 0; si < names->methods[i].nb_statements; si++) {
            
                struct statement_info *stmt = &names->methods[i].statements[si];
                
                if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_STRING_CONCAT) {
                    insns_size += 0;
                } else if (statement_is_field_string_call (names, stmt)) {
                    insns_size += 14;
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_RESULT_CALL) {
                    insns_size += 4;
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_MULTI) {
                    insns_size += 3;
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_RESULT_MULTI) {
                    insns_size += 4 + count_instance_result_call_move_units (&names->methods[i], stmt);
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_NONE) {
                    insns_size += 3;
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_STRING) {
                
                    if (!stmt->target && (names->methods[i].flags & METHOD_STATIC)) {
                        insns_size += 10;
                    } else {
                        insns_size += 5;
                    }
                
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_PARAM) {
                
                    if (!stmt->target && (names->methods[i].flags & METHOD_STATIC)) {
                        insns_size += 8;
                    } else {
                        insns_size += 3;
                    }
                
                } else if (stmt->kind == STMT_LOCAL) {
                
                    if (stmt->arg_kind == ARG_THIS) {
                        insns_size += 2;
                    } else if (stmt->arg_kind == ARG_NEW) {
                        insns_size += 5;
                    } else if (stmt->arg_kind == ARG_NEW_ARRAY) {
                    
                        unsigned long dst_reg = method_local_register (&names->methods[i], strtoul (stmt->target + 1, 0, 10));
                        unsigned long size_reg = method_local_register (&names->methods[i], strtoul (stmt->method + 1, 0, 10));
                        
                        insns_size += dex_move_value_code_units (1, size_reg) + 2 + dex_move_value_code_units (dst_reg, 0);
                    
                    } else if (stmt->arg_kind == ARG_ARRAY_GET) {
                        insns_size += stmt->method[0] == '$' || stmt->method[0] == '#' ? 2 : 4;
                    } else if (stmt->arg_kind == ARG_ARRAY_LENGTH) {
                        insns_size += stmt->method[0] == '$' || stmt->method[0] == '#' ? 1 : 3;
                    } else if (stmt->arg_kind == ARG_STRING) {
                        insns_size += 2;
                    } else if (stmt->arg_kind == ARG_INT) {
                    
                        const char *target_type = stmt->target[0] == '$' ? names->methods[i].locals[strtoul (stmt->target + 1, 0, 10)].type : find_field_type (names, stmt->target);
                        insns_size += target_type && (target_type[0] == 'J' || target_type[0] == 'D') ? 5 : 3;
                    
                    } else {
                    
                        const char *source_type = arithmetic_source_type (names, &names->methods[i], stmt->method);
                        insns_size += arithmetic_source_load_units (stmt->method) + count_arithmetic_code_units (&names->methods[i], source_type, stmt->arg_text);
                        
                        if (stmt->target[0] == '$' || stmt->target[0] == '#') {
                            insns_size++;
                        }
                    
                    }
                    
                    if (stmt->target[0] != '$' && stmt->target[0] != '#') {
                        insns_size += 2;
                    }
                
                } else if (stmt->kind == STMT_ARRAY_PUT) {
                    insns_size += 2;
                } else if (stmt->kind == STMT_THROW) {
                
                    unsigned long throw_reg;
                    
                    if (stmt->target[0] == '$') {
                        throw_reg = method_local_register (&names->methods[i], strtoul (stmt->target + 1, 0, 10));
                    } else {
                        throw_reg = method_param_register (&names->methods[i], registers_size, ins_size, strtoul (stmt->target + 1, 0, 10));
                    }
                    
                    dex_u2 (&buf, 0x0027 | ((unsigned int) throw_reg << 8));
                
                } else if (stmt->kind == STMT_RETURN) {
                
                    if (stmt->arg_kind == ARG_CALL) {
                        insns_size += 5;
                    } else if (stmt->arg_kind == ARG_STRING) {
                        insns_size += 2;
                    } else if (stmt->arg_kind == ARG_INT) {
                        insns_size += 4;
                    } else if (stmt->arg_kind == ARG_ARITHMETIC) {
                    
                        const char *source_type;
                        
                        if (stmt->target[0] == '#') {
                            source_type = method_param_type (&names->methods[i], strtoul (stmt->target + 1, 0, 10));
                        } else if (stmt->target[0] == '$') {
                            source_type = names->methods[i].locals[strtoul (stmt->target + 1, 0, 10)].type;
                        } else {
                            source_type = find_field_type (names, stmt->target);
                        }
                        
                        insns_size += 2 + count_arithmetic_code_units (&names->methods[i], source_type, stmt->arg_text);
                        
                        if (stmt->target[0] != '#') {
                            insns_size++;
                        }
                    
                    } else {
                        insns_size++;
                    }
                    
                    has_return = 1;
                
                }
            
            }
            
            if (!has_return) {
            
                if (strcmp (names->methods[i].return_type, "V") != 0) {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "missing return value");
                    return 1;
                
                }
                
                insns_size++;
            
            }
            
            dex_write_code_item_header (&buf, registers_size, ins_size, 2, insns_size);
            insns_start_pos = dex_pos (&buf);
            
            for (si = 0; si < names->methods[i].nb_statements; si++) {
            
                struct statement_info *stmt = &names->methods[i].statements[si];
                
                if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_STRING_CONCAT) {
                
                    unsigned long call_idx = stmt->target ? find_static_method_id_single (names, method_id_idx, stmt->target, stmt->method) : find_static_method_id_single (names, method_id_idx, names->class_name, stmt->method);
                    
                    if (call_idx == METHOD_NOT_FOUND && stmt->target) {
                    
                        char *target_desc = resolve_class_type (names, stmt->target);
                        call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, "V", declared_reference_call_parameter (target_desc, stmt->method));
                        
                        free (target_desc);
                    
                    }
                    
                    if (call_idx == METHOD_ACCESS_DENIED) {
                    
                        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "private method '%s.%s' is not accessible from '%s'", stmt->target ? stmt->target : names->class_name, stmt->method, names->class_name);
                        return 1;
                    
                    }
                    
                    if (call_idx == METHOD_NOT_FOUND) {
                    
                        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown static method '%s.%s'", stmt->target ? stmt->target : names->class_name, stmt->method);
                        return 1;
                    
                    }
                    
                    report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret,
                        "runtime string concatenation is not implemented");
                    
                    return 1;

                } else if (statement_is_field_string_call (names, stmt)) {
                
                    const char *field_type = find_field_type (names, stmt->target);
                    char *class_name = descriptor_to_simple_class_name (field_type);
                    
                    unsigned long type_idx = find_type_idx (types, nb_types, field_type);
                    unsigned long field_idx = find_field_id_single (names, field_id_idx, stmt->target);
                    unsigned long init_idx = find_static_method_id_single (names, method_id_idx, class_name ? class_name : stmt->target, "<init>");
                    unsigned long call_idx = find_static_method_id_single (names, method_id_idx, class_name ? class_name : stmt->target, stmt->method);
                    unsigned long str_idx = dex_find_string (strings, nb_strings, stmt->arg_text);
                    
                    if (init_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_ACCESS_DENIED) {
                    
                        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "private method '%s.%s' is not accessible from '%s'", class_name ? class_name : stmt->target, stmt->method, names->class_name);
                        
                        free (class_name);
                        return 1;
                    
                    }
                    
                    if (field_idx == METHOD_NOT_FOUND || init_idx == METHOD_NOT_FOUND || call_idx == METHOD_NOT_FOUND) {
                    
                        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown field method '%s.%s'", stmt->target, stmt->method);
                        
                        free (class_name);
                        return 1;
                    
                    }
                    
                    dex_u2 (&buf, 0x0022);
                    dex_u2 (&buf, type_idx);
                    dex_u2 (&buf, 0x1070);
                    dex_u2 (&buf, init_idx);
                    dex_u2 (&buf, 0x0000);
                    dex_u2 (&buf, 0x0067);
                    dex_u2 (&buf, field_idx);
                    dex_u2 (&buf, 0x0062);
                    dex_u2 (&buf, field_idx);
                    dex_u2 (&buf, 0x011A);
                    dex_u2 (&buf, str_idx);
                    dex_u2 (&buf, 0x206E);
                    dex_u2 (&buf, call_idx);
                    dex_u2 (&buf, dex_invoke_regs2 (0, 1));
                    
                    free (class_name);
                
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_MULTI && (!stmt->target || stmt->target[0] != '@')) {
                
                    char *params = 0;
                    
                    unsigned long *arg_locals = 0, nb_arg_locals = 0, ai;
                    unsigned long call_idx = METHOD_NOT_FOUND;
                    
                    unsigned int regs[5], reg_count = 0;
                    char reg_types[5] = { 0, 0, 0, 0, 0 };
                    
                    int invoke_static = stmt->target != 0;
                    
                    if (split_constructor_arguments (stmt->arg_text, &params, &arg_locals, &nb_arg_locals)) {
                        return 1;
                    }
                    
                    call_idx = find_static_method_id_single_params (names, method_id_idx,   stmt->target ? stmt->target : names->class_name, stmt->method, params);
                    
                    if (call_idx != METHOD_NOT_FOUND) {
                    
                        unsigned long mi;
                        
                        for (mi = 0; mi < names->nb_methods; mi++) {
                        
                            if (method_id_idx[mi] == call_idx) {
                            
                                invoke_static = (names->methods[mi].flags & METHOD_STATIC) != 0;
                                break;
                            
                            }
                        
                        }
                    
                    } else if (stmt->target) {
                    
                        char *target_desc = resolve_class_type (names, stmt->target);
                        
                        call_idx = find_external_method_id (external_methods, nb_external_methods,
                            target_desc, stmt->method, "V", params);
                        
                        free (target_desc);
                    
                    }
                    
                    if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                    
                        char *argument_types = format_argument_types (params);
                        
                        report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret,
                            "no overload of '%s' matches argument types %s", stmt->method, argument_types);
                        
                        free (argument_types);
                        free (arg_locals);
                        free (params);
                        
                        return 1;
                    
                    }
                    
                    if (!invoke_static) {
                    
                        reg_types[reg_count] = 'L';
                        regs[reg_count++] = (unsigned int) (registers_size - ins_size);
                    
                    }
                    
                    for (ai = 0; ai < nb_arg_locals; ai++) {
                    
                        const char *arg_type = names->methods[i].locals[arg_locals[ai]].type;
                        unsigned int r = (unsigned int) method_local_register (&names->methods[i], arg_locals[ai]);
                        
                        if (reg_count >= 5 || ((arg_type[0] == 'J' || arg_type[0] == 'D') && reg_count >= 4)) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret,
                                "too many method argument registers");
                            
                            free (arg_locals);
                            free (params);
                            
                            return 1;
                        
                        }
                        
                        reg_types[reg_count] = arg_type[0];
                        regs[reg_count++] = r;
                        
                        if (arg_type[0] == 'J' || arg_type[0] == 'D') {
                            regs[reg_count++] = r + 1;
                        }
                    
                    }
                    
                    for (ai = 0; ai < reg_count; ai++) {
                    
                        if (regs[ai] > 15) {
                        
                            unsigned long ri;
                            
                            for (ri = 0; ri < reg_count; ri++) {
                            
                                if (reg_types[ri]) {
                                    dex_write_move_value (&buf, reg_types[ri], ri, regs[ri]);
                                }
                                
                                regs[ri] = (unsigned int) ri;
                            
                            }
                            
                            break;
                        
                        }
                    
                    }
                    
                    dex_u2 (&buf, (invoke_static ? 0x0071 : 0x006e) | ((reg_count & 0x0f) << 12) | ((reg_count == 5 ? regs[4] : 0) << 8));
                    dex_u2 (&buf, call_idx);
                    
                    dex_u2 (&buf, dex_invoke_regs4 (reg_count > 0 ? regs[0] : 0, reg_count > 1 ? regs[1] : 0, reg_count > 2 ? regs[2] : 0, reg_count > 3 ? regs[3] : 0));
                    
                    free (arg_locals);
                    free (params);
                
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_RESULT_MULTI) {
                
                    unsigned long *arg_locals = 0, nb_arg_locals = 0, ai;
                    char *params = 0;
                    
                    unsigned long call_idx = METHOD_NOT_FOUND;
                    int instance_call = stmt->target && stmt->target[0] == '$';
                    
                    unsigned int regs[5], reg_count = 0;
                    
                    char reg_types[5] = { 0, 0, 0, 0, 0 };
                    unsigned long result_reg = method_local_register (&names->methods[i], stmt->arg_param_index);
                    
                    const char *return_type = names->methods[i].locals[stmt->arg_param_index].type;
                    
                    if (split_constructor_arguments (stmt->arg_text, &params, &arg_locals, &nb_arg_locals)) {
                        return 1;
                    }
                    
                    if (instance_call) {
                    
                        unsigned long receiver_index = strtoul (stmt->target + 1, 0, 10);
                        
                        const char *target_desc = names->methods[i].locals[receiver_index].type;
                        char *class_desc = dex_class_descriptor (names);
                        
                        if (strcmp (target_desc, class_desc) == 0) {
                        
                            unsigned long callee_index;
                            
                            for (callee_index = 0; callee_index < names->nb_methods; callee_index++) {
                            
                                struct method_info *callee = &names->methods[callee_index];
                                
                                if (!(callee->flags & METHOD_STATIC) && strcmp (callee->java_name, stmt->method) == 0 && strcmp (callee->params, params) == 0 && strcmp (callee->return_type, return_type) == 0) {
                                
                                    call_idx = method_id_idx[callee_index];
                                    break;
                                
                                }
                            
                            }
                        
                        } else {
                            call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, return_type, params);
                        }
                        
                        free (class_desc);
                        
                        reg_types[reg_count] = 'L';
                        regs[reg_count++] = (unsigned int) method_local_register (&names->methods[i], receiver_index);
                    
                    } else {
                    
                        call_idx = find_static_method_id_single_params (names, method_id_idx, stmt->target, stmt->method, params);
                        
                        if (call_idx == METHOD_NOT_FOUND && stmt->target && !target_is_single_source_class (names, stmt->target)) {
                        
                            char *target_desc = resolve_class_type (names, stmt->target);
                            call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, return_type, params);
                            
                            free (target_desc);
                        
                        }
                    
                    }
                    
                    if (call_idx == METHOD_NOT_FOUND) {
                    
                        report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "no overload of '%s' matches method arguments", stmt->method);
                        
                        free (arg_locals);
                        free (params);
                        
                        return 1;
                    
                    }
                    
                    for (ai = 0; ai < nb_arg_locals; ai++) {
                    
                        const char *arg_type = names->methods[i].locals[arg_locals[ai]].type;
                        unsigned int reg = (unsigned int) method_local_register (&names->methods[i], arg_locals[ai]);
                        
                        if (reg_count >= 5 || ((arg_type[0] == 'J' || arg_type[0] == 'D') && reg_count >= 4)) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "too many method argument registers");
                            
                            free (arg_locals);
                            free (params);
                            
                            return 1;
                        
                        }
                        
                        reg_types[reg_count] = arg_type[0];
                        regs[reg_count++] = reg;
                        
                        if (arg_type[0] == 'J' || arg_type[0] == 'D') {
                            regs[reg_count++] = reg + 1;
                        }
                    
                    }
                    
                    for (ai = 0; ai < reg_count; ai++) {
                    
                        if (regs[ai] > 15) {
                        
                            unsigned long ri;
                            
                            for (ri = 0; ri < reg_count; ri++) {
                            
                                if (reg_types[ri]) {
                                    dex_write_move_value (&buf, reg_types[ri], ri, regs[ri]);
                                }
                                
                                regs[ri] = (unsigned int) ri;
                            
                            }
                            
                            break;
                        
                        }
                    
                    }
                    
                    dex_u2 (&buf, (instance_call ? 0x006e : 0x0071) | ((reg_count & 0x0f) << 12) | ((reg_count == 5 ? regs[4] : 0) << 8));
                    dex_u2 (&buf, call_idx);
                    
                    dex_u2 (&buf, dex_invoke_regs4 (reg_count > 0 ? regs[0] : 0, reg_count > 1 ? regs[1] : 0, reg_count > 2 ? regs[2] : 0, reg_count > 3 ? regs[3] : 0));
                    dex_u2 (&buf, dex_move_result_opcode (return_type, result_reg));
                    
                    free (arg_locals);
                    free (params);
                
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_RESULT_CALL && stmt->target && stmt->target[0] != '$') {
                
                    unsigned long result_index = strtoul (stmt->arg_text + 1, 0, 10);
                    unsigned long result_reg = method_local_register (&names->methods[i], result_index);
                    
                    const char *return_type = names->methods[i].locals[result_index].type;
                    const char *target_desc = resolve_class_type (names, stmt->target);
                    
                    unsigned long call_idx;
                    
                    if (!target_desc) {
                    
                        report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start,
                            statement_target_caret (stmt), "unknown class '%s'", stmt->target);
                        return 1;
                    
                    }
                    
                    call_idx = find_external_method_id (external_methods, nb_external_methods,
                        target_desc, stmt->method, return_type, "");
                    
                    if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                    
                        report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start,
                            stmt->caret, "unknown static method '%s.%s'", stmt->target, stmt->method);
                        return 1;
                    
                    }
                    
                    dex_u2 (&buf, 0x0071);
                    dex_u2 (&buf, call_idx);
                    dex_u2 (&buf, 0);
                    dex_u2 (&buf, dex_move_result_opcode (return_type, result_reg));
                
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_RESULT_CALL && stmt->target && stmt->target[0] == '$') {
                    
                    unsigned long local_index = strtoul (stmt->target + 1, 0, 10);
                    unsigned long result_index = strtoul (stmt->arg_text + 1, 0, 10);
                    unsigned long object_reg = method_local_register (&names->methods[i], local_index);
                    unsigned long result_reg = method_local_register (&names->methods[i], result_index);
                    
                    const char *target_desc = names->methods[i].locals[local_index].type;
                    const char *return_type = names->methods[i].locals[result_index].type;
                    
                    unsigned long call_idx = METHOD_NOT_FOUND;
                    char *class_desc = dex_class_descriptor (names);
                    
                    if (strcmp (target_desc, class_desc) == 0) {
                    
                        unsigned long callee_index;
                        
                        for (callee_index = 0; callee_index < names->nb_methods; callee_index++) {
                        
                            struct method_info *callee = &names->methods[callee_index];
                            
                            if (!(callee->flags & METHOD_STATIC) && strcmp (callee->java_name, stmt->method) == 0 && strcmp (callee->params, "") == 0 && strcmp (callee->return_type, return_type) == 0) {
                            
                                call_idx = method_id_idx[callee_index];
                                break;
                            
                            }
                        
                        }
                    
                    } else {
                        call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, return_type, "");
                    }
                    
                    free (class_desc);
                    
                    if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                    
                        report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "unknown instance method '%s'", stmt->method);
                        return 1;
                    
                    }
                    
                    dex_u2 (&buf, object_reg < 16 ? 0x106E : 0x0174);
                    dex_u2 (&buf, call_idx);
                    
                    dex_u2 (&buf, (unsigned int) object_reg);
                    dex_u2 (&buf, dex_move_result_opcode (return_type, result_reg));
                
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_NONE && stmt->target && stmt->target[0] == '$') {
                
                    unsigned long local_index = strtoul (stmt->target + 1, 0, 10);
                    unsigned long object_reg = method_local_register (&names->methods[i], local_index);
                    
                    const char *target_desc = names->methods[i].locals[local_index].type;
                    unsigned long call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, "V", "");
                    
                    if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                    
                        report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "unknown instance method '%s'", stmt->method);
                        return 1;
                    
                    }
                    
                    dex_u2 (&buf, 0x106E);
                    dex_u2 (&buf, call_idx);
                    
                    dex_u2 (&buf, ((unsigned int) object_reg & 0x0F));
                
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_STRING && !find_field_type (names, stmt->target)) {
                
                    unsigned long str_idx = dex_find_string (strings, nb_strings, stmt->arg_text);
                    unsigned long call_idx = stmt->target ? find_static_method_id_single (names, method_id_idx, stmt->target, stmt->method) : find_static_method_id_single (names, method_id_idx, names->class_name, stmt->method);
                    
                    if (call_idx == METHOD_NOT_FOUND && stmt->target) {
                    
                        char *target_desc = resolve_class_type (names, stmt->target);
                        call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, "V", declared_reference_call_parameter (target_desc, stmt->method));
                        
                        free (target_desc);
                    
                    }
                    
                    if (call_idx == METHOD_ACCESS_DENIED) {
                    
                        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "private method '%s.%s' is not accessible from '%s'", stmt->target ? stmt->target : names->class_name, stmt->method, names->class_name);
                        return 1;
                    
                    }
                    
                    if (call_idx == METHOD_NOT_FOUND) {
                    
                        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown static method '%s.%s'", stmt->target ? stmt->target : names->class_name, stmt->method);
                        return 1;
                    
                    }
                    
                    dex_u2 (&buf, 0x001A);
                    dex_u2 (&buf, str_idx);
                    
                    dex_u2 (&buf, 0x1071);
                    dex_u2 (&buf, call_idx);
                    
                    dex_u2 (&buf, 0x0000);
                
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_PARAM && !find_field_type (names, stmt->target)) {
                
                    unsigned long arg_reg = method_param_register (&names->methods[i], registers_size, ins_size, stmt->arg_param_index);
                    unsigned long call_idx;
                    
                    if (!stmt->target && !(names->methods[i].flags & METHOD_STATIC)) {
                    
                        unsigned long this_reg = registers_size - ins_size;
                        call_idx = find_static_method_id_single (names, method_id_idx, names->class_name, stmt->method);
                        
                        if (call_idx == METHOD_NOT_FOUND) {
                        
                            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown method '%s'", stmt->method);
                            return 1;
                        
                        }
                        
                        dex_u2 (&buf, 0x206E);
                        dex_u2 (&buf, call_idx);
                        
                        dex_u2 (&buf, dex_invoke_regs2 ((unsigned int) this_reg, (unsigned int) arg_reg));
                    
                    } else if (!stmt->target) {
                    
                        unsigned long init_idx;
                        
                        call_idx = find_static_method_id_single (names, method_id_idx, names->class_name, stmt->method);
                        init_idx = find_static_method_id_single (names, method_id_idx, names->class_name, "<init>");
                        
                        if (call_idx == METHOD_NOT_FOUND || init_idx == METHOD_NOT_FOUND) {
                        
                            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown method '%s'", stmt->method);
                            return 1;
                        
                        }
                        
                        dex_u2 (&buf, 0x0022);
                        dex_u2 (&buf, class_type_idx);
                        dex_u2 (&buf, 0x1070);
                        dex_u2 (&buf, init_idx);
                        dex_u2 (&buf, 0x0000);
                        dex_u2 (&buf, 0x206E);
                        dex_u2 (&buf, call_idx);
                        dex_u2 (&buf, dex_invoke_regs2 (0, (unsigned int) arg_reg));
                    
                    } else {
                    
                        call_idx = find_static_method_id_single (names, method_id_idx, stmt->target, stmt->method);
                        
                        if (call_idx == METHOD_NOT_FOUND) {
                        
                            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown static method '%s.%s'", stmt->target, stmt->method);
                            return 1;
                        
                        }
                        
                        dex_u2 (&buf, 0x1071);
                        dex_u2 (&buf, call_idx);
                        dex_u2 (&buf, ((unsigned int) arg_reg & 0x0F));
                    
                    }
                
                } else if (stmt->kind == STMT_LOCAL) {
                
                    int parameter_assignment = stmt->target[0] == '#';
                    int field_assignment = stmt->target[0] != '$' && !parameter_assignment;
                    
                    unsigned long local_index = field_assignment || parameter_assignment ? 0 : strtoul (stmt->target + 1, 0, 10);
                    unsigned long dst_reg = field_assignment ? 0 : parameter_assignment ? method_param_register (&names->methods[i], registers_size, ins_size, strtoul (stmt->target + 1, 0, 10)) : method_local_register (&names->methods[i], local_index);
                    
                    const char *local_type = field_assignment ? find_field_info_by_name (names, stmt->target)->type : parameter_assignment ? method_param_type (&names->methods[i], strtoul (stmt->target + 1, 0, 10)) : names->methods[i].locals[local_index].type;
                    
                    if (stmt->arg_kind == ARG_THIS) {
                    
                        unsigned long this_reg = registers_size - ins_size;
                        dex_write_move_value (&buf, 'L', dst_reg, this_reg);
                    
                    } else if (stmt->arg_kind == ARG_NEW) {
                    
                        char *class_name = descriptor_to_simple_class_name (local_type);
                        unsigned long type_idx = find_type_idx (types, nb_types, local_type);
                        
                        char *ctor_params = 0;
                        
                        unsigned long *ctor_locals = 0, nb_ctor_locals = 0;
                        unsigned long init_idx;
                        
                        unsigned int regs[5];
                        unsigned int reg_count = 1;
                        
                        unsigned long ai;
                        
                        if (split_constructor_arguments (stmt->arg_text, &ctor_params, &ctor_locals, &nb_ctor_locals)) {
                            return 1;
                        }
                        
                        init_idx = find_static_method_id_single_params (names, method_id_idx, class_name ? class_name : stmt->method, "<init>", ctor_params);
                        
                        if (init_idx == METHOD_NOT_FOUND) {
                            init_idx = find_external_method_id (external_methods, nb_external_methods, local_type, "<init>", "V", ctor_params);
                        }
                        
                        if (init_idx == METHOD_ACCESS_DENIED || init_idx == METHOD_NOT_FOUND) {
                        
                            char *argument_types = format_argument_types (ctor_params);
                            
                            report_line_at (stmt->filename ? stmt->filename : get_filename (),
                                stmt->line ? stmt->line : get_line_number (), REPORT_ERROR,
                                stmt->start ? stmt->start : tok.start,
                                stmt->caret ? stmt->caret : tok.caret,
                                "no overload of '%s' matches argument types %s", stmt->method, argument_types);
                            
                            free (argument_types);
                            free (ctor_params);
                            free (ctor_locals);
                            free (class_name);
                            
                            return 1;
                        
                        }
                        
                        regs[0] = (unsigned int) dst_reg;
                        
                        for (ai = 0; ai < nb_ctor_locals; ai++) {
                        
                            const char *arg_type = names->methods[i].locals[ctor_locals[ai]].type;
                            unsigned int r = (unsigned int) method_local_register (&names->methods[i], ctor_locals[ai]);
                            
                            if (reg_count >= 5 || ((arg_type[0] == 'J' || arg_type[0] == 'D') && reg_count >= 4)) {
                            
                                report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "too many constructor argument registers");
                                
                                free (ctor_params);
                                free (ctor_locals);
                                
                                free (class_name);
                                return 1;
                            
                            }
                            
                            regs[reg_count++] = r;
                            
                            if (arg_type[0] == 'J' || arg_type[0] == 'D') {
                                regs[reg_count++] = r + 1;
                            }
                        
                        }
                        
                        dex_u2 (&buf, 0x0022 | ((unsigned int) dst_reg << 8));
                        dex_u2 (&buf, type_idx);
                        dex_u2 (&buf, 0x0070 | ((reg_count & 0x0f) << 12) | ((reg_count == 5 ? regs[4] : 0) << 8));
                        dex_u2 (&buf, init_idx);
                        dex_u2 (&buf, dex_invoke_regs4 (regs[0], reg_count > 1 ? regs[1] : 0, reg_count > 2 ? regs[2] : 0, reg_count > 3 ? regs[3] : 0));
                        
                        free (ctor_params);
                        free (ctor_locals);
                        free (class_name);
                    
                    } else if (stmt->arg_kind == ARG_NEW_ARRAY) {
                    
                        unsigned long size_local = strtoul (stmt->method + 1, 0, 10);
                        unsigned long size_reg = method_local_register (&names->methods[i], size_local);
                        unsigned long type_idx = find_type_idx (types, nb_types, local_type);
                        
                        dex_write_move_value (&buf, 'I', 1, size_reg);
                        
                        dex_u2 (&buf, 0x1023);
                        dex_u2 (&buf, type_idx);
                        
                        dex_write_move_value (&buf, '[', dst_reg, 0);
                    
                    } else if (stmt->arg_kind == ARG_ARRAY_LENGTH) {
                    
                        unsigned long array_reg;
                        
                        if (stmt->method[0] == '$') {
                        
                            unsigned long array_local = strtoul (stmt->method + 1, 0, 10);
                            array_reg = method_local_register (&names->methods[i], array_local);
                        
                        } else if (stmt->method[0] == '#') {
                        
                            unsigned long param_index = strtoul (stmt->method + 1, 0, 10);
                            array_reg = method_param_register (&names->methods[i], registers_size, ins_size, param_index);
                        
                        } else {
                        
                            struct field_info *array_field = find_field_info_by_name (names, stmt->method);
                            unsigned long array_idx = find_field_id_single (names, field_id_idx, stmt->method);
                            
                            array_reg = dst_reg;
                            
                            if (array_field->flags & METHOD_STATIC) {
                                dex_u2 (&buf, dex_sget_opcode_for_type (array_field->type) | ((unsigned int) array_reg << 8));
                            } else {
                            
                                unsigned long this_reg = registers_size - ins_size;
                                dex_u2 (&buf, dex_iget_opcode_for_type (array_field->type) | ((unsigned int) array_reg << 8) | ((unsigned int) this_reg << 12));
                            
                            }
                            
                            dex_u2 (&buf, array_idx);
                        
                        }
                        
                        dex_u2 (&buf, 0x0021 | ((unsigned int) dst_reg << 8) | ((unsigned int) array_reg << 12));
                    
                    } else if (stmt->arg_kind == ARG_INT) {
                        dex_write_const_integer (&buf, local_type, dst_reg, stmt->arg_text);
                    } else if (stmt->arg_kind == ARG_STRING) {
                    
                        unsigned long str_idx = dex_find_string (strings, nb_strings, stmt->arg_text);
                        dex_u2 (&buf, 0x001a | ((unsigned int) dst_reg << 8));
                        dex_u2 (&buf, str_idx);
                    
                    } else {
                    
                        const char *source_type = arithmetic_source_type (names, &names->methods[i], stmt->method);
                        unsigned long work_reg = 0;
                        
                        if (field_initializer_is_call (stmt->method)) {
                        
                            char *target = 0;
                            char *called_method = 0;
                            unsigned long call_idx;
                            
                            if (split_field_call_initializer (stmt->method, &target, &called_method)) {
                                return 1;
                            }
                            
                            call_idx = find_static_method_id_single_typed (names, method_id_idx,
                                target, called_method, "D");
                            
                            if (call_idx == METHOD_NOT_FOUND) {
                            
                                struct external_method_info *external_method;
                                char *target_desc = resolve_class_type (names, target);
                                
                                call_idx = find_external_method_id (external_methods,
                                    nb_external_methods, target_desc, called_method, "D", "");
                                
                                if (call_idx == METHOD_NOT_FOUND) {
                                
                                    external_method = find_external_method (external_methods,
                                        nb_external_methods, target_desc, called_method, "");
                                    
                                    if (external_method) {
                                    
                                        call_idx = external_method->method_id_idx;
                                        source_type = external_method->return_type;
                                    
                                    }
                                
                                }
                                
                                free (target_desc);
                            
                            }
                            
                            if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                            
                                report_line_at (stmt->filename ? stmt->filename : get_filename (),
                                    stmt->line ? stmt->line : get_line_number (), REPORT_ERROR,
                                        stmt->start ? stmt->start : tok.start,
                                            stmt->caret ? stmt->caret : tok.caret,
                                                "unknown static method '%s.%s'", target, called_method);
                                
                                free (target);
                                free (called_method);
                                
                                return 1;
                            
                            }
                            
                            dex_u2 (&buf, 0x0071);
                            dex_u2 (&buf, call_idx);
                            dex_u2 (&buf, 0x0000);
                            
                            dex_u2 (&buf, dex_move_result_opcode (source_type, work_reg));
                            
                            free (target);
                            free (called_method);
                        
                        } else if (stmt->method[0] == '#') {
                        
                            unsigned long param_index = strtoul (stmt->method + 1, 0, 10);
                            unsigned long source_reg = method_param_register (&names->methods[i], registers_size, ins_size, param_index);
                            dex_write_move_value (&buf, source_type[0], work_reg, source_reg);
                        
                        } else if (stmt->method[0] == '$') {
                        
                            unsigned long source_reg = method_local_register (&names->methods[i], strtoul (stmt->method + 1, 0, 10));
                            dex_write_move_value (&buf, source_type[0], work_reg, source_reg);
                        
                        } else {
                        
                            struct field_info *source_field = find_field_info_by_name (names, stmt->method);
                            unsigned long source_idx = find_field_id_single (names, field_id_idx, stmt->method);
                            
                            if (source_field->flags & METHOD_STATIC) {
                                dex_u2 (&buf, dex_sget_opcode_for_type (source_type) | ((unsigned int) work_reg << 8));
                            } else {
                            
                                unsigned long this_reg = registers_size - ins_size;
                                dex_u2 (&buf, dex_iget_opcode_for_type (source_type) | ((unsigned int) work_reg << 8) | ((unsigned int) this_reg << 12));
                            
                            }
                            
                            dex_u2 (&buf, source_idx);
                        
                        }
                        
                        dex_write_mul_factor_list (&buf, &names->methods[i], source_type, local_type, work_reg, stmt->arg_text);
                        
                        if (!field_assignment && dst_reg != work_reg) {
                            dex_write_move_value (&buf, local_type[0], dst_reg, work_reg);
                        }
                    
                    }
                    
                    if (field_assignment) {
                    
                        struct field_info *dest_field = find_field_info_by_name (names, stmt->target);
                        unsigned long dest_idx = find_field_id_single (names, field_id_idx, stmt->target);
                        
                        if (dest_field->flags & METHOD_STATIC) {
                            dex_u2 (&buf, dex_sput_opcode_for_type (dest_field->type) | ((unsigned int) dst_reg << 8));
                        } else {
                        
                            unsigned long this_reg = registers_size - ins_size;
                            dex_u2 (&buf, dex_iput_opcode_for_type (dest_field->type) | ((unsigned int) dst_reg << 8) | ((unsigned int) this_reg << 12));
                        
                        }
                        
                        dex_u2 (&buf, dest_idx);
                    
                    }
                
                } else if (stmt->kind == STMT_RETURN) {
                
                    if (strcmp (names->methods[i].return_type, "V") == 0) {
                        dex_u2 (&buf, 0x000E);
                    } else if (stmt->arg_kind == ARG_THIS) {
                    
                        unsigned long this_reg = registers_size - ins_size;
                        dex_u2 (&buf, dex_return_opcode (names->methods[i].return_type, this_reg));
                    
                    } else if (stmt->arg_kind == ARG_CALL) {
                    
                        unsigned long call_idx = stmt->target ? find_static_method_id_single (names, method_id_idx, stmt->target, stmt->method)
                            : find_static_method_id_single (names, method_id_idx, names->class_name, stmt->method);
                        
                        if (call_idx == METHOD_NOT_FOUND && stmt->target) {
                        
                            char *target_desc = resolve_class_type (names, stmt->target);
                            call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, names->methods[i].return_type, "");
                            
                            free (target_desc);
                        
                        }
                        
                        if (call_idx == METHOD_ACCESS_DENIED) {
                        
                            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "private method '%s.%s' is not accessible from '%s'", stmt->target ? stmt->target : names->class_name, stmt->method, names->class_name);
                            return 1;
                        
                        }
                        
                        if (call_idx == METHOD_NOT_FOUND) {
                        
                            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown static method '%s.%s'", stmt->target ? stmt->target : names->class_name, stmt->method);
                            return 1;
                        
                        }
                        
                        dex_u2 (&buf, 0x0071);
                        dex_u2 (&buf, call_idx);
                        dex_u2 (&buf, 0x0000);
                        
                        dex_u2 (&buf, dex_move_result_opcode (names->methods[i].return_type, 0));
                        dex_u2 (&buf, dex_return_opcode (names->methods[i].return_type, 0));
                    
                    } else if (stmt->arg_kind == ARG_STRING) {
                    
                        unsigned long str_idx = dex_find_string (strings, nb_strings, stmt->arg_text);
                        
                        dex_u2 (&buf, 0x001A);
                        dex_u2 (&buf, str_idx);
                        
                        dex_u2 (&buf, dex_return_opcode (names->methods[i].return_type, 0));
                    
                    } else if (stmt->arg_kind == ARG_INT) {
                    
                        dex_write_const_integer (&buf, names->methods[i].return_type, 0, stmt->arg_text);
                        dex_u2 (&buf, dex_return_opcode (names->methods[i].return_type, 0));
                    
                    } else if (stmt->arg_kind == ARG_ARITHMETIC) {
                    
                        const char *source_type;
                        
                        if (stmt->target[0] == '#') {
                        
                            unsigned long param_index = strtoul (stmt->target + 1, 0, 10);
                            unsigned long source_reg = method_param_register (&names->methods[i], registers_size, ins_size, param_index);
                            
                            source_type = method_param_type (&names->methods[i], param_index);
                            dex_u2 (&buf, 0x0001 | ((unsigned int) source_reg << 12));
                        
                        } else if (stmt->target[0] == '$') {
                        
                            unsigned long local_index = strtoul (stmt->target + 1, 0, 10);
                            unsigned long source_reg = method_local_register (&names->methods[i], local_index);
                            
                            source_type = names->methods[i].locals[local_index].type;
                            dex_write_move_value (&buf, source_type[0], 0, source_reg);
                        
                        } else {
                        
                            struct field_info *source_field = find_field_info_by_name (names, stmt->target);
                            unsigned long source_idx = find_field_id_single (names, field_id_idx, stmt->target);
                            
                            source_type = source_field->type;
                            
                            if (source_field->flags & METHOD_STATIC) {
                                dex_u2 (&buf, dex_sget_opcode_for_type (source_type));
                            } else {
                            
                                unsigned long this_reg = registers_size - ins_size;
                                dex_u2 (&buf, dex_iget_opcode_for_type (source_type) | ((unsigned int) this_reg << 12));
                            
                            }
                            
                            dex_u2 (&buf, source_idx);
                        
                        }
                        
                        dex_write_mul_factor_list (&buf, &names->methods[i], source_type, names->methods[i].return_type, 0, stmt->arg_text);
                        dex_u2 (&buf, dex_return_opcode (names->methods[i].return_type, 0));
                    
                    } else {
                    
                        unsigned long return_reg = method_param_register (&names->methods[i], registers_size, ins_size, stmt->arg_param_index);
                        dex_u2 (&buf, dex_return_opcode (names->methods[i].return_type, return_reg));
                    
                    }
                
                }
            
            }
            
            if (!has_return) {
                dex_u2 (&buf, 0x000E);
            }
            
            dex_u4_at (&buf, method_code_off[i] + 12, (dex_pos (&buf) - insns_start_pos) / 2);
        
        }
    
    }
    
    static_values_off = dex_write_static_values (&buf, names, field_id_idx, strings, nb_strings);
    dex_align4 (&buf);
    
    class_data_off = dex_pos (&buf);
    
    dex_uleb128 (&buf, nb_static_fields);
    dex_uleb128 (&buf, nb_instance_fields);
    dex_uleb128 (&buf, names->nb_methods);
    dex_uleb128 (&buf, 0);
    
    {
    
        unsigned long prev_field = 0;
        int have_prev = 0;
        
        for (i = 0; i < names->nb_fields; i++) {
        
            if (!(names->fields[i].flags & METHOD_STATIC)) {
                continue;
            }
            
            dex_uleb128 (&buf, have_prev ? field_id_idx[i] - prev_field : field_id_idx[i]);
            dex_uleb128 (&buf, field_access_flags (&names->fields[i]));
            
            prev_field = field_id_idx[i];
            have_prev = 1;
        
        }
        
        prev_field = 0;
        have_prev = 0;
        
        for (i = 0; i < names->nb_fields; i++) {
        
            if (names->fields[i].flags & METHOD_STATIC) {
                continue;
            }
            
            dex_uleb128 (&buf, have_prev ? field_id_idx[i] - prev_field : field_id_idx[i]);
            dex_uleb128 (&buf, field_access_flags (&names->fields[i]));
            
            prev_field = field_id_idx[i];
            have_prev = 1;
        
        }
    
    }
    
    for (i = 0; i < names->nb_methods; i++) {
    
        dex_uleb128 (&buf, i == 0 ? method_id_idx[i] : method_id_idx[i] - method_id_idx[i - 1]);
        dex_uleb128 (&buf, names->methods[i].flags);
        dex_uleb128 (&buf, method_code_off[i]);
    
    }
    
    dex_align4 (&buf);
    map_off = dex_pos (&buf);
    
    dex_u4 (&buf, 8 + (nb_fields ? 1 : 0) + (nb_type_lists ? 1 : 0) + (static_values_off ? 1 : 0));
    
    dex_u2 (&buf, 0x0000); dex_u2 (&buf, 0); dex_u4 (&buf, 1); dex_u4 (&buf, 0);
    dex_u2 (&buf, 0x0001); dex_u2 (&buf, 0); dex_u4 (&buf, nb_strings); dex_u4 (&buf, string_ids_off);
    dex_u2 (&buf, 0x0002); dex_u2 (&buf, 0); dex_u4 (&buf, nb_types); dex_u4 (&buf, type_ids_off);
    dex_u2 (&buf, 0x0003); dex_u2 (&buf, 0); dex_u4 (&buf, nb_protos); dex_u4 (&buf, proto_ids_off);
    
    if (nb_fields) {
        dex_u2 (&buf, 0x0004); dex_u2 (&buf, 0); dex_u4 (&buf, nb_fields); dex_u4 (&buf, field_ids_off);
    }
    
    dex_u2 (&buf, 0x0005); dex_u2 (&buf, 0); dex_u4 (&buf, nb_method_ids); dex_u4 (&buf, method_ids_off);
    dex_u2 (&buf, 0x0006); dex_u2 (&buf, 0); dex_u4 (&buf, 1); dex_u4 (&buf, class_defs_off);
    
    if (nb_type_lists) {
        dex_u2 (&buf, 0x1001); dex_u2 (&buf, 0); dex_u4 (&buf, nb_type_lists); dex_u4 (&buf, type_lists_off);
    }
    
    dex_u2 (&buf, 0x2000); dex_u2 (&buf, 0); dex_u4 (&buf, 1); dex_u4 (&buf, class_data_off);
    dex_u2 (&buf, 0x2001); dex_u2 (&buf, 0); dex_u4 (&buf, nb_code_items); dex_u4 (&buf, code_items_off);
    
    if (static_values_off) {
        dex_u2 (&buf, 0x2005); dex_u2 (&buf, 0); dex_u4 (&buf, 1); dex_u4 (&buf, static_values_off);
    }
    
    memcpy (buf.data, "dex\n035\0", 8);
    
    dex_u4_at (&buf, 32, buf.size);
    dex_u4_at (&buf, 36, header_size);
    dex_u4_at (&buf, 40, 0x12345678UL);
    dex_u4_at (&buf, 44, 0);
    dex_u4_at (&buf, 48, 0);
    dex_u4_at (&buf, 52, map_off);
    dex_u4_at (&buf, 56, nb_strings);
    dex_u4_at (&buf, 60, string_ids_off);
    dex_u4_at (&buf, 64, nb_types);
    dex_u4_at (&buf, 68, type_ids_off);
    dex_u4_at (&buf, 72, nb_protos);
    dex_u4_at (&buf, 76, proto_ids_off);
    dex_u4_at (&buf, 80, nb_fields);
    dex_u4_at (&buf, 84, nb_fields ? field_ids_off : 0);
    dex_u4_at (&buf, 88, nb_method_ids);
    dex_u4_at (&buf, 92, method_ids_off);
    dex_u4_at (&buf, 96, 1);
    dex_u4_at (&buf, 100, class_defs_off);
    dex_u4_at (&buf, 104, buf.size - data_off);
    dex_u4_at (&buf, 108, data_off);
    
    for (i = 0; i < nb_strings; i++) {
        dex_u4_at (&buf, string_ids_off + i * 4, string_offsets[i]);
    }
    
    type_string_idx = xmalloc (nb_types * sizeof (*type_string_idx));
    
    for (i = 0; i < nb_types; i++) {
    
        type_string_idx[i] = dex_find_string (strings, nb_strings, types[i]);
        dex_u4_at (&buf, type_ids_off + i * 4, type_string_idx[i]);
    
    }
    
    for (i = 0; i < names->nb_methods; i++) {
    
        unsigned long ret_type_idx = find_type_idx (types, nb_types, names->methods[i].return_type);
        unsigned long shorty_idx = dex_find_string (strings, nb_strings, names->methods[i].shorty);
        
        dex_u4_at (&buf, proto_ids_off + i * 12 + 0, shorty_idx);
        dex_u4_at (&buf, proto_ids_off + i * 12 + 4, ret_type_idx);
        dex_u4_at (&buf, proto_ids_off + i * 12 + 8, method_params_off[i]);
    
    }
    
    for (i = 0; i < nb_external_methods; i++) {
    
        unsigned long proto = external_methods[i].proto_idx;
        unsigned long ret_type_idx = find_type_idx (types, nb_types, external_methods[i].return_type);
        unsigned long shorty_idx = dex_find_string (strings, nb_strings, external_methods[i].shorty);
        
        dex_u4_at (&buf, proto_ids_off + proto * 12 + 0, shorty_idx);
        dex_u4_at (&buf, proto_ids_off + proto * 12 + 4, ret_type_idx);
        dex_u4_at (&buf, proto_ids_off + proto * 12 + 8, external_methods[i].params_off);
    
    }
    
    {
    
        unsigned long proto = names->nb_methods + nb_external_methods;
        unsigned long ret_type_idx = find_type_idx (types, nb_types, "V");
        unsigned long shorty_idx = dex_find_string (strings, nb_strings, "V");
        
        dex_u4_at (&buf, proto_ids_off + proto * 12 + 0, shorty_idx);
        dex_u4_at (&buf, proto_ids_off + proto * 12 + 4, ret_type_idx);
        dex_u4_at (&buf, proto_ids_off + proto * 12 + 8, 0);
    
    }
    
    for (i = 0; i < names->nb_fields; i++) {
    
        unsigned long off = field_ids_off + field_id_idx[i] * 8;
        
        dex_u2_at (&buf, off + 0, class_type_idx);
        dex_u2_at (&buf, off + 2, field_type_idx[i]);
        dex_u4_at (&buf, off + 4, field_name_idx[i]);
    
    }
    
    for (i = 0; i < names->nb_methods; i++) {
    
        unsigned long off = method_ids_off + i * 8;
        
        dex_u2_at (&buf, off + 0, class_type_idx);
        dex_u2_at (&buf, off + 2, method_proto_idx[i]);
        dex_u4_at (&buf, off + 4, method_name_idx[i]);
    
    }
    
    for (i = 0; i < nb_external_methods; i++) {
    
        unsigned long off = method_ids_off + external_methods[i].method_id_idx * 8;
        
        dex_u2_at (&buf, off + 0, external_methods[i].class_type_idx);
        dex_u2_at (&buf, off + 2, external_methods[i].proto_idx);
        dex_u4_at (&buf, off + 4, external_methods[i].name_idx);
    
    }
    
    dex_u4_at (&buf, class_defs_off + 0, class_type_idx);
    dex_u4_at (&buf, class_defs_off + 4, class_access_flags (names));
    dex_u4_at (&buf, class_defs_off + 8, super_type_idx);
    dex_u4_at (&buf, class_defs_off + 12, interface_type_list_off);
    dex_u4_at (&buf, class_defs_off + 16, 0xFFFFFFFFUL);
    dex_u4_at (&buf, class_defs_off + 20, 0);
    dex_u4_at (&buf, class_defs_off + 24, class_data_off);
    dex_u4_at (&buf, class_defs_off + 28, static_values_off);
    
    dex_finish_hashes (&buf);
    
    if (fwrite (buf.data, 1, buf.size, fp) != buf.size) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "failed to write dex file");
        return 1;
    
    }
    
    free_external_methods (external_methods, nb_external_methods);
    
    free (field_id_idx);
    free (field_type_idx);
    free (field_name_idx);
    free (method_code_off);
    free (type_string_idx);
    free (string_offsets);
    free (method_params_off);
    free (method_id_idx);
    free (method_name_idx);
    free (method_proto_idx);
    free (types);
    
    for (i = 0; i < nb_strings; i++) {
        free ((char *) strings[i].text);
    }
    
    free (strings);
    free (class_desc);
    free (buf.data);
    
    return 0;

}

static unsigned long find_static_method_id_multi (struct source_names *all_names, struct multi_method_map *method_map, unsigned long nb_real_methods, unsigned long *method_id_idx, unsigned long caller_source_index, const char *target, const char *name) {

    unsigned long i;
    
    if (!target || !name) {
        return METHOD_NOT_FOUND;
    }
    
    for (i = 0; i < nb_real_methods; i++) {
    
        struct source_names *names = &all_names[method_map[i].source_index];
        struct method_info *method = &names->methods[method_map[i].method_index];
        
        if (strcmp (names->class_name, target) == 0 && strcmp (method->java_name, name) == 0) {
        
            if ((method->flags & METHOD_PRIVATE) && method_map[i].source_index != caller_source_index) {
                return METHOD_ACCESS_DENIED;
            }
            
            return method_id_idx[i];
        
        }
    
    }
    
    return METHOD_NOT_FOUND;

}

static unsigned long find_static_method_id_multi_params (struct source_names *all_names, struct multi_method_map *method_map, unsigned long nb_real_methods, unsigned long *method_id_idx, unsigned long caller_source_index, const char *target, const char *name, const char *params) {

    unsigned long i;
    
    if (!target || !name || !params) {
        return METHOD_NOT_FOUND;
    }
    
    for (i = 0; i < nb_real_methods; i++) {
    
        struct source_names *names = &all_names[method_map[i].source_index];
        struct method_info *method = &names->methods[method_map[i].method_index];
        
        if (strcmp (names->class_name, target) == 0 && strcmp (method->java_name, name) == 0
            && strcmp (method->params, params) == 0 && (method->flags & METHOD_STATIC)) {
        
            if ((method->flags & METHOD_PRIVATE) && method_map[i].source_index != caller_source_index) {
                return METHOD_ACCESS_DENIED;
            }
            
            return method_id_idx[i];
        
        }
    
    }
    
    return METHOD_NOT_FOUND;

}

static const char *find_field_type (struct source_names *names, const char *field_name) {

    struct field_info *field;
    
    if (!field_name) {
        return 0;
    }
    
    field = find_field_info_by_name (names, field_name);
    return field ? field->type : 0;

}

static char *descriptor_to_simple_class_name (const char *desc) {

    const char *start;
    const char *end;
    const char *slash;
    
    unsigned long len;
    char *name;
    
    if (!desc || desc[0] != 'L') {
        return 0;
    }
    
    start = desc + 1;
    end = strchr (start, ';');
    
    if (!end) {
        return 0;
    }
    
    slash = end;
    
    while (slash > start && slash[-1] != '/') {
        slash--;
    }
    
    len = (unsigned long) (end - slash);
    
    name = xmalloc (len + 1);
    memcpy (name, slash, len);
    
    name[len] = '\0';
    return name;

}

static unsigned long find_method_id_multi_by_class_name (struct source_names *all_names, struct multi_method_map *method_map, unsigned long nb_real_methods, unsigned long *method_id_idx, unsigned long caller_source_index, const char *class_name, const char *name) {

    unsigned long i;
    
    for (i = 0; i < nb_real_methods; i++) {
    
        struct source_names *names = &all_names[method_map[i].source_index];
        struct method_info *method = &names->methods[method_map[i].method_index];
        
        if (strcmp (names->class_name, class_name) == 0 && strcmp (method->java_name, name) == 0) {
        
            if ((method->flags & METHOD_PRIVATE) && method_map[i].source_index != caller_source_index) {
                return METHOD_ACCESS_DENIED;
            }
            
            return method_id_idx[i];
        
        }
    
    }
    
    return METHOD_NOT_FOUND;

}

static unsigned long find_method_id_multi_by_class_name_typed (struct source_names *all_names, struct multi_method_map *method_map, unsigned long nb_real_methods, unsigned long *method_id_idx, unsigned long caller_source_index, const char *class_name, const char *name, const char *return_type) {

    unsigned long i;
    
    for (i = 0; i < nb_real_methods; i++) {
    
        struct source_names *names = &all_names[method_map[i].source_index];
        struct method_info *method = &names->methods[method_map[i].method_index];
        
        if (strcmp (names->class_name, class_name) == 0 && strcmp (method->java_name, name) == 0 && strcmp (method->return_type, return_type) == 0) {
        
            if ((method->flags & METHOD_PRIVATE) && method_map[i].source_index != caller_source_index) {
                return METHOD_ACCESS_DENIED;
            }
            
            return method_id_idx[i];
        
        }
    
    }
    
    return METHOD_NOT_FOUND;

}

static unsigned long find_source_index_by_descriptor (struct source_names *all_names, unsigned long nb_sources, const char *desc) {

    unsigned long i;
    
    if (!desc) {
        return METHOD_NOT_FOUND;
    }
    
    for (i = 0; i < nb_sources; i++) {
    
        char *class_desc = dex_class_descriptor (&all_names[i]);
        int match = strcmp (class_desc, desc) == 0;
        
        free (class_desc);
        
        if (match) {
            return i;
        }
    
    }
    
    return METHOD_NOT_FOUND;

}

static int class_target_available (struct source_names *all_names, unsigned long nb_sources, struct source_names *names, const char *target) {

    char *target_desc;
    int available;
    
    target_desc = resolve_class_type (names, target);
    
    available = strcmp (target_desc, JAVA_OBJECT_TYPE) == 0 || strcmp (target_desc, JAVA_STRING_TYPE) == 0
        || find_source_index_by_descriptor (all_names, nb_sources, target_desc) != METHOD_NOT_FOUND;
    
    free (target_desc);
    return available;

}

static const char *statement_target_caret (struct statement_info *stmt) {

    const char *target;

    if (!stmt->start || !stmt->target) {
        return stmt->caret;
    }

    target = strstr (stmt->start, stmt->target);
    return target ? target : stmt->caret;

}

static int source_extends_source (struct source_names *all_names, unsigned long nb_sources, unsigned long child_index, unsigned long parent_index) {

    unsigned long cur = child_index;
    unsigned long guard = 0;
    
    while (cur != METHOD_NOT_FOUND && cur < nb_sources && guard++ < nb_sources) {
    
        const char *super_desc = all_names[cur].super_class;
        
        if (!super_desc) {
            return 0;
        }
        
        cur = find_source_index_by_descriptor (all_names, nb_sources, super_desc);
        
        if (cur == parent_index) {
            return 1;
        }
    
    }
    
    return 0;

}

static int method_access_allowed_multi (struct source_names *all_names, unsigned long nb_sources, unsigned long caller_source_index, unsigned long owner_source_index, struct method_info *method) {

    if (owner_source_index == caller_source_index) {
        return 1;
    }
    
    if (method->flags & METHOD_PRIVATE) {
        return 0;
    }
    
    if (method->flags & METHOD_PROTECTED) {
        return source_extends_source (all_names, nb_sources, caller_source_index, owner_source_index);
    }
    
    return 1;

}

static unsigned long find_instance_method_id_multi_from_source_params (struct source_names *all_names, unsigned long nb_sources, struct multi_method_map *method_map, unsigned long nb_real_methods, unsigned long *method_id_idx, unsigned long caller_source_index, unsigned long start_source_index, const char *name, const char *params) {

    unsigned long guard, cur;
    int pass;
    
    for (pass = 0; pass < 2; pass++) {
    
        cur = start_source_index;
        guard = 0;
        
        while (cur != METHOD_NOT_FOUND && cur < nb_sources && guard++ < nb_sources) {
        
            unsigned long i;
            
            for (i = 0; i < nb_real_methods; i++) {
            
                struct source_names *owner = &all_names[method_map[i].source_index];
                struct method_info *method = &owner->methods[method_map[i].method_index];
                
                if (method_map[i].source_index == cur && strcmp (method->java_name, name) == 0 && (pass == 0 ? strcmp (method->params, params) == 0 : method_params_match_int_widening (params, method->params))) {
                
                    if (method->flags & METHOD_STATIC) {
                        continue;
                    }
                    
                    if (!method_access_allowed_multi (all_names, nb_sources, caller_source_index, cur, method)) {
                        return METHOD_ACCESS_DENIED;
                    }
                    
                    return method_id_idx[i];
                
                }
            
            }
            
            if (!all_names[cur].super_class) {
                break;
            }
            
            cur = find_source_index_by_descriptor (all_names, nb_sources, all_names[cur].super_class);
        
        }
    
    }
    
    return METHOD_NOT_FOUND;

}

static unsigned long find_instance_method_id_multi_from_source (struct source_names *all_names, unsigned long nb_sources, struct multi_method_map *method_map, unsigned long nb_real_methods, unsigned long *method_id_idx, unsigned long caller_source_index, unsigned long start_source_index, const char *name) {

    unsigned long cur = start_source_index;
    unsigned long guard = 0;
    
    while (cur != METHOD_NOT_FOUND && cur < nb_sources && guard++ < nb_sources) {
    
        unsigned long i;
        
        for (i = 0; i < nb_real_methods; i++) {
        
            struct source_names *owner = &all_names[method_map[i].source_index];
            struct method_info *method = &owner->methods[method_map[i].method_index];
            
            if (method_map[i].source_index == cur && strcmp (method->java_name, name) == 0) {
            
                if (method->flags & METHOD_STATIC) {
                    continue;
                }
                
                if (!method_access_allowed_multi (all_names, nb_sources, caller_source_index, cur, method)) {
                    return METHOD_ACCESS_DENIED;
                }
                
                return method_id_idx[i];
            
            }
        
        }
        
        if (!all_names[cur].super_class) {
            break;
        }
        
        cur = find_source_index_by_descriptor (all_names, nb_sources, all_names[cur].super_class);
    
    }
    
    return METHOD_NOT_FOUND;

}

static unsigned long find_inherited_static_method_id_multi_from_source (struct source_names *all_names, unsigned long nb_sources, struct multi_method_map *method_map, unsigned long nb_real_methods, unsigned long *method_id_idx, unsigned long caller_source_index, unsigned long start_source_index, const char *name) {

    unsigned long cur = start_source_index;
    unsigned long guard = 0;
    
    while (cur != METHOD_NOT_FOUND && cur < nb_sources && guard++ < nb_sources) {
    
        unsigned long i;
        
        for (i = 0; i < nb_real_methods; i++) {
        
            struct source_names *owner = &all_names[method_map[i].source_index];
            struct method_info *method = &owner->methods[method_map[i].method_index];
            
            if (method_map[i].source_index == cur && strcmp (method->java_name, name) == 0) {
            
                if (!(method->flags & METHOD_STATIC)) {
                    continue;
                }
                
                if (!method_access_allowed_multi (all_names, nb_sources, caller_source_index, cur, method)) {
                    return METHOD_ACCESS_DENIED;
                }
                
                return method_id_idx[i];
            
            }
        
        }
        
        if (!all_names[cur].super_class) {
            break;
        }
        
        cur = find_source_index_by_descriptor (all_names, nb_sources, all_names[cur].super_class);
    
    }
    
    return METHOD_NOT_FOUND;

}

static int class_declares_concrete_method (struct source_names *names, const char *name, const char *params) {

    unsigned long i;
    
    for (i = 0; i < names->nb_methods; i++) {
    
        if (strcmp (names->methods[i].java_name, name) == 0 && (!params || strcmp (names->methods[i].params, params) == 0)) {
        
            if (!(names->methods[i].flags & METHOD_ABSTRACT)) {
                return 1;
            }
        
        }
    
    }
    
    return 0;

}

static int interface_declares_abstract_method_multi (struct source_names *all_names, unsigned long nb_sources, unsigned long iface_source_index, const char *name, const char *params) {

    unsigned long i;
    
    for (i = 0; i < all_names[iface_source_index].nb_methods; i++) {
    
        struct method_info *method = &all_names[iface_source_index].methods[i];
        
        if (strcmp (method->java_name, name) == 0 && (!params || strcmp (method->params, params) == 0)) {
        
            if (method->flags & METHOD_ABSTRACT) {
                return 1;
            }
        
        }
    
    }
    
    for (i = 0; i < all_names[iface_source_index].nb_interfaces; i++) {
    
        unsigned long parent = find_source_index_by_descriptor (all_names, nb_sources, all_names[iface_source_index].interfaces[i]);
        
        if (parent != METHOD_NOT_FOUND && interface_declares_abstract_method_multi (all_names, nb_sources, parent, name, params)) {
            return 1;
        }
    
    }
    
    return 0;

}

static int missing_implemented_abstract_method_multi (struct source_names *all_names, unsigned long nb_sources, unsigned long source_index, const char *name, const char *params) {

    unsigned long i;
    
    if (class_declares_concrete_method (&all_names[source_index], name, params)) {
        return 0;
    }
    
    for (i = 0; i < all_names[source_index].nb_interfaces; i++) {
    
        unsigned long iface = find_source_index_by_descriptor (all_names, nb_sources, all_names[source_index].interfaces[i]);
        
        if (iface != METHOD_NOT_FOUND && interface_declares_abstract_method_multi (all_names, nb_sources, iface, name, params)) {
            return 1;
        }
    
    }
    
    return 0;

}

static int statement_is_field_string_call (struct source_names *names, struct statement_info *stmt) {

    return stmt->kind == STMT_CALL
        && stmt->arg_kind == ARG_STRING
            && stmt->target
                && find_field_type (names, stmt->target) != 0;

}

static unsigned long find_field_id_single (struct source_names *names, unsigned long *field_id_idx, const char *field_name) {

    unsigned long i;
    
    for (i = 0; i < names->nb_fields; i++) {
    
        if (strcmp (names->fields[i].name, field_name) == 0) {
            return field_id_idx[i];
        }
    
    }
    
    return 0xFFFFFFFFUL;

}

static unsigned long find_field_id_multi (struct source_names *all_names, struct multi_field_map *field_map, unsigned long nb_real_fields, unsigned long *field_id_idx, unsigned long source_index, const char *field_name) {

    struct field_info *field = find_field_info_by_name (&all_names[source_index], field_name);
    unsigned long i, j, k;
    
    for (i = 0; i < nb_real_fields; i++) {
    
        struct field_info *candidate = &all_names[field_map[i].source_index].fields[field_map[i].field_index];
        
        if (field_map[i].source_index == source_index && strcmp (candidate->name, field_name) == 0) {
            return field_id_idx[i];
        }
    
    }
    
    for (j = 0; j < nb_declaration_names; j++) {
    
        struct source_names *declaration = &declaration_names[j];
        char *owner_desc;
        
        for (k = 0; k < declaration->nb_fields; k++) {
        
            if (&declaration->fields[k] == field) {
                break;
            }
        
        }
        
        if (k == declaration->nb_fields) {
            continue;
        }
        
        owner_desc = dex_class_descriptor (declaration);
        
        for (i = 0; i < nb_real_fields; i++) {
        
            struct source_names *owner = &all_names[field_map[i].source_index];
            char *candidate_desc = dex_class_descriptor (owner);
            
            int match = strcmp (owner_desc, candidate_desc) == 0
                && strcmp (owner->fields[field_map[i].field_index].name, field_name) == 0;
            
            free (candidate_desc);
            
            if (match) {
            
                free (owner_desc);
                return field_id_idx[i];
            
            }
        
        }
        
        free (owner_desc);
        break;
    
    }
    
    return METHOD_NOT_FOUND;

}

static int collect_source_names (const char *src_path, struct source_names *names) {

    int ret;
    
    if (init_tokenizer (src_path)) {
        return 1;
    }
    
    ret = scan_source_names (names);
    
    if (!ret) {
        ret = check_class_filename (src_path, names->class_name);
    }
    
    if (state->ifp && state->ifp != stdin) {
    
        fclose (state->ifp);
        state->ifp = 0;
    
    }
    
    return ret;

}

static int write_dex_files (FILE *fp, struct source_names *all_names, unsigned long nb_sources) {

    struct dex_buf buf;
    struct dex_string_ref *strings = 0;
    struct multi_field_map *field_map = 0;
    struct multi_method_map *method_map = 0;
    struct external_method_info *external_methods = 0;
    
    char **class_descs;
    char **super_descs;
    
    unsigned long nb_strings = 0;
    unsigned long nb_external_methods = 0;
    
    unsigned long *string_offsets;
    unsigned long *type_string_idx;
    
    const char **types = 0;
    unsigned long nb_types = 0;
    
    unsigned long *interface_type_list_off;
    unsigned long *class_type_idx;
    unsigned long *super_type_idx;
    unsigned long *method_proto_idx;
    unsigned long *method_name_idx;
    unsigned long *method_id_idx;
    unsigned long *method_params_off;
    unsigned long *method_code_off;
    unsigned long *field_name_idx;
    unsigned long *field_type_idx;
    unsigned long *field_id_idx;
    
    unsigned long string_ids_off, type_ids_off, proto_ids_off, field_ids_off, method_ids_off, class_defs_off, data_off;
    unsigned long i, j, k, m, mi;
    unsigned long header_size = 0x70;
    unsigned long map_off;
    unsigned long nb_type_lists = 0, type_lists_off = 0;
    
    unsigned long nb_fields = 0;
    unsigned long nb_method_ids;
    unsigned long nb_protos;
    unsigned long nb_code_items = 0, code_items_off = 0;
    unsigned long nb_real_methods = 0;
    unsigned long nb_real_fields = 0;
    unsigned long nb_static_value_arrays = 0;
    unsigned long first_static_values_off = 0;
    unsigned long *class_data_off;
    unsigned long *static_values_off;
    
    if (nb_sources == 1) {
    
        unsigned long mi;
        unsigned long si;
        
        int needs_control_writer = 0;
        
        for (mi = 0; mi < all_names[0].nb_methods && !needs_control_writer; mi++) {
        
            for (si = 0; si < all_names[0].methods[mi].nb_statements; si++) {
            
                int kind = all_names[0].methods[mi].statements[si].kind;
                
                if (kind == STMT_GOTO || kind == STMT_IF_FALSE || kind == STMT_IF_EQ_CONST || kind == STMT_MOVE_EXCEPTION || kind == STMT_ARRAY_PUT
                    || (kind == STMT_LOCAL && (all_names[0].methods[mi].statements[si].arg_kind == ARG_ARRAY_GET
                        || all_names[0].methods[mi].statements[si].arg_kind == ARG_INSTANCEOF
                            || all_names[0].methods[mi].statements[si].arg_kind == ARG_CHECKCAST
                                || all_names[0].methods[mi].statements[si].arg_kind == ARG_STATIC_GET))) {
                
                    needs_control_writer = 1;
                    break;
                
                }
            
            }
        
        }
        
        if (!needs_control_writer) {
            return write_dex_file (fp, &all_names[0]);
        }
    
    }
    
    memset (&buf, 0, sizeof (buf));
    
    class_descs = xmalloc (nb_sources * sizeof (*class_descs));
    super_descs = xmalloc (nb_sources * sizeof (*super_descs));
    
    class_type_idx = xmalloc (nb_sources * sizeof (*class_type_idx));
    super_type_idx = xmalloc (nb_sources * sizeof (*super_type_idx));
    
    interface_type_list_off = xmalloc (nb_sources * sizeof (*interface_type_list_off));
    class_data_off = xmalloc (nb_sources * sizeof (*class_data_off));
    static_values_off = xmalloc (nb_sources * sizeof (*static_values_off));
    
    for (i = 0; i < nb_sources; i++) {
    
        for (j = 0; j < all_names[i].nb_methods; j++) {
        
            struct method_info *caller = &all_names[i].methods[j];
            
            for (k = 0; k < caller->nb_statements; k++) {
            
                struct statement_info *stmt = &caller->statements[k];
                
                if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_RESULT_CALL
                    && stmt->target && stmt->arg_text && stmt->arg_text[0] == '$') {
                
                    unsigned long receiver_index = stmt->target[0] == '$' ? strtoul (stmt->target + 1, 0, 10) : 0;
                    unsigned long result_index = strtoul (stmt->arg_text + 1, 0, 10);
                    
                    const char *target_desc = stmt->target[0] == '$'
                        ? caller->locals[receiver_index].type
                            : resolve_class_type (&all_names[i], stmt->target);
                    
                    unsigned long target_source = target_desc
                        ? find_source_index_by_descriptor (all_names, nb_sources, target_desc)
                            : METHOD_NOT_FOUND;
                    
                    if (target_source != METHOD_NOT_FOUND) {
                    
                        unsigned long method_index;
                        
                        for (method_index = 0; method_index < all_names[target_source].nb_methods; method_index++) {
                        
                            struct method_info *called = &all_names[target_source].methods[method_index];
                            
                            if (strcmp (called->java_name, stmt->method) == 0 && strcmp (called->params, "") == 0) {
                            
                                free (caller->locals[result_index].type);
                                
                                caller->locals[result_index].type = xstrdup (called->return_type);
                                break;
                            
                            }
                        
                        }
                    
                    }
                
                }
            
            }
        
        }
    
    }
    
    for (i = 0; i < nb_sources; i++) {
        
        class_descs[i] = dex_class_descriptor (&all_names[i]);
        super_descs[i] = all_names[i].super_class;
        
        interface_type_list_off[i] = 0;
        
        nb_real_methods += all_names[i].nb_methods;
        nb_real_fields += all_names[i].nb_fields;
    
    }
    
    method_map = xmalloc (nb_real_methods * sizeof (*method_map));
    method_proto_idx = xmalloc (nb_real_methods * sizeof (*method_proto_idx));
    method_name_idx = xmalloc (nb_real_methods * sizeof (*method_name_idx));
    method_id_idx = xmalloc (nb_real_methods * sizeof (*method_id_idx));
    method_params_off = xmalloc (nb_real_methods * sizeof (*method_params_off));
    method_code_off = xmalloc (nb_real_methods * sizeof (*method_code_off));
    
    field_map = xmalloc (nb_real_fields ? nb_real_fields * sizeof (*field_map) : sizeof (*field_map));
    field_name_idx = xmalloc (nb_real_fields ? nb_real_fields * sizeof (*field_name_idx) : sizeof (*field_name_idx));
    field_type_idx = xmalloc (nb_real_fields ? nb_real_fields * sizeof (*field_type_idx) : sizeof (*field_type_idx));
    field_id_idx = xmalloc (nb_real_fields ? nb_real_fields * sizeof (*field_id_idx) : sizeof (*field_id_idx));
    
    for (i = 0; i < nb_sources; i++) {
    
        dex_add_string (&strings, &nb_strings, class_descs[i]);
        
        if (super_descs[i]) {
            dex_add_string (&strings, &nb_strings, super_descs[i]);
        }
        
        for (j = 0; j < all_names[i].nb_interfaces; j++) {
            dex_add_string (&strings, &nb_strings, all_names[i].interfaces[j]);
        }
    
    }
    
    dex_add_string (&strings, &nb_strings, "<init>");
    dex_add_string (&strings, &nb_strings, "V");
    
    mi = 0;
    m = 0;
    
    for (i = 0; i < nb_sources; i++) {
    
        for (j = 0; j < all_names[i].nb_fields; j++) {
        
            field_map[m].source_index = i;
            field_map[m].field_index = j;
            
            dex_add_string (&strings, &nb_strings, all_names[i].fields[j].name);
            dex_add_string (&strings, &nb_strings, all_names[i].fields[j].type);
            
            if (all_names[i].fields[j].has_initializer && all_names[i].fields[j].is_constructor_initializer) {
            
                if (all_names[i].fields[j].init_text[5] == 'S') {
                    dex_add_string (&strings, &nb_strings, all_names[i].fields[j].init_text + 6);
                }
                
                {
                
                    const char *params = all_names[i].fields[j].init_text[5] == 'S' ? declared_reference_call_parameter (all_names[i].fields[j].type, "<init>") : "";
                    
                    if (!params) {
                        report_field_init_error (&all_names[i].fields[j], "constructor parameter type cannot be resolved from source declarations");
                        return 1;
                    }
                    
                    add_external_method (&external_methods, &nb_external_methods, all_names[i].fields[j].type, "<init>", "V", params);
                
                }
            
            } else if (all_names[i].fields[j].has_initializer && (all_names[i].fields[j].type[0] == 'L')) {
                dex_add_string (&strings, &nb_strings, all_names[i].fields[j].init_text);
            } else if (field_initializer_is_array (all_names[i].fields[j].type, all_names[i].fields[j].init_text)) {
            
                unsigned long count = field_array_count (all_names[i].fields[j].init_text);
                unsigned long k;
                
                const char *p = strchr (all_names[i].fields[j].init_text, '|');
                
                for (k = 0; k < count; k++) {
                
                    char *value = 0;
                    
                    if (!(p = field_array_next (p, &value))) {
                        return 1;
                    }
                    
                    dex_add_string (&strings, &nb_strings, value);
                    free (value);
                
                }
            
            }
            
            if (all_names[i].fields[j].has_initializer && field_initializer_is_mul_di (all_names[i].fields[j].init_text)) {
            
                char *target = 0;
                char *method = 0;
                char *factor = 0;
                
                if (split_field_mul_di_initializer (all_names[i].fields[j].init_text, &target, &method, &factor)) {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "bad multiplied method call field initializer");
                    return 1;
                
                }
                
                if (!target_is_multi_source_class (all_names, nb_sources, target)) {
                
                    char *target_desc = resolve_class_type (&all_names[i], target);
                    add_external_method (&external_methods, &nb_external_methods, target_desc, method, "D", "");
                    
                    free (target_desc);
                
                }
                
                free (target);
                free (method);
                free (factor);
            
            } else if (all_names[i].fields[j].has_initializer && field_initializer_is_call (all_names[i].fields[j].init_text)) {
            
                char *target = 0;
                char *method = 0;
                
                if (split_field_call_initializer (all_names[i].fields[j].init_text, &target, &method)) {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "bad method call field initializer");
                    return 1;
                
                }
                
                if (!target_is_multi_source_class (all_names, nb_sources, target)) {
                
                    char *target_desc = resolve_class_type (&all_names[i], target);
                    add_external_method (&external_methods, &nb_external_methods, target_desc, method, all_names[i].fields[j].type, "");
                    
                    free (target_desc);
                
                }
                
                free (target);
                free (method);
            
            }
            
            m++;
        
        }
        
        for (j = 0; j < all_names[i].nb_methods; j++) {
        
            const char *p;
            
            method_map[mi].source_index = i;
            method_map[mi].method_index = j;
            
            dex_add_string (&strings, &nb_strings, all_names[i].methods[j].name);
            dex_add_string (&strings, &nb_strings, all_names[i].methods[j].return_type);
            dex_add_string (&strings, &nb_strings, all_names[i].methods[j].shorty);
            
            for (k = 0; k < all_names[i].methods[j].nb_locals; k++) {
                dex_add_string (&strings, &nb_strings, all_names[i].methods[j].locals[k].type);
            }
            
            for (k = 0; k < all_names[i].methods[j].nb_statements; k++) {
            
                struct statement_info *stmt = &all_names[i].methods[j].statements[k];
                
                if (stmt->kind == STMT_LOCAL && (stmt->arg_kind == ARG_INSTANCEOF || stmt->arg_kind == ARG_CHECKCAST)) {
                    dex_add_string (&strings, &nb_strings, stmt->arg_text);
                } else if ((stmt->kind == STMT_CALL || stmt->kind == STMT_RETURN || stmt->kind == STMT_LOCAL) && stmt->arg_kind == ARG_STRING) {
                    dex_add_string (&strings, &nb_strings, stmt->arg_text);
                } else if ((stmt->kind == STMT_CALL || stmt->kind == STMT_LOCAL) && stmt->arg_kind == ARG_STRING_CONCAT) {
                
                    const char *cp = stmt->arg_text;
                    
                    while (cp && *cp) {
                    
                        const char *cq = strchr (cp, '');
                        
                        if (!cq) {
                            cq = cp + strlen (cp);
                        }
                        
                        if (cq > cp && cp[0] == 'S' && cp[1] == ':') {
                        
                            unsigned long clen = (unsigned long) (cq - (cp + 2));
                            char *ctmp = xmalloc (clen + 1);
                            
                            memcpy (ctmp, cp + 2, clen);
                            ctmp[clen] = '\0';
                            
                            dex_add_string (&strings, &nb_strings, ctmp);
                        
                        }
                        
                        cp = (*cq == '') ? cq + 1 : cq;
                    
                    }
                
                }
                
                if (statement_is_field_string_call (&all_names[i], stmt)) {
                    continue;
                } else if (stmt->kind == STMT_CALL && stmt->target && stmt->target[0] == '$') {
                
                    unsigned long local_index = strtoul (stmt->target + 1, 0, 10);
                    const char *target_desc = all_names[i].methods[j].locals[local_index].type;
                    
                    if (find_source_index_by_descriptor (all_names, nb_sources, target_desc) == METHOD_NOT_FOUND) {
                    
                        const char *return_type = "V";
                        
                        const char *params = "";
                        char *call_params = 0;
                        
                        unsigned long *call_locals = 0, nb_call_locals = 0;
                        
                        if (stmt->arg_kind == ARG_RESULT_CALL && stmt->arg_text && stmt->arg_text[0] == '$') {
                            return_type = all_names[i].methods[j].locals[strtoul (stmt->arg_text + 1, 0, 10)].type;
                        } else if (stmt->arg_kind == ARG_RESULT_MULTI) {
                        
                            return_type = all_names[i].methods[j].locals[stmt->arg_param_index].type;
                            
                            if (split_constructor_arguments (stmt->arg_text, &call_params, &call_locals, &nb_call_locals)) {
                                return 1;
                            }
                            
                            params = call_params;
                        
                        }
                        
                        add_external_method (&external_methods, &nb_external_methods, target_desc, stmt->method, return_type, params);
                        
                        free (call_params);
                        free (call_locals);
                    
                    }
                
                } else if (stmt->kind == STMT_CALL && stmt->target
                    && strcmp (stmt->target, "@this") != 0 && strcmp (stmt->target, "@super") != 0
                        && !target_is_multi_source_class (all_names, nb_sources, stmt->target)) {
                
                    const char *params = "";
                    const char *return_type = "V";
                    
                    char *call_params = 0;
                    unsigned long *call_locals = 0, nb_call_locals = 0;
                    
                    char *target_desc = resolve_class_type (&all_names[i], stmt->target);
                    
                    if (stmt->arg_kind == ARG_STRING || stmt->arg_kind == ARG_STRING_CONCAT) {
                    
                        params = declared_reference_call_parameter (target_desc, stmt->method);
                        
                        if (!params) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret,
                                "method parameter type cannot be resolved from source declarations");
                            free (target_desc);
                            return 1;
                        }
                    
                    }
                    
                    if (stmt->arg_kind == ARG_RESULT_MULTI || stmt->arg_kind == ARG_MULTI) {
                    
                        if (split_constructor_arguments (stmt->arg_text, &call_params, &call_locals, &nb_call_locals)) {
                        
                            free (target_desc);
                            return 1;
                        
                        }
                        
                        params = call_params;
                    
                    }
                    
                    if (stmt->arg_kind == ARG_RESULT_MULTI) {
                        return_type = all_names[i].methods[j].locals[stmt->arg_param_index].type;
                    }
                    
                    add_external_method (&external_methods, &nb_external_methods, target_desc, stmt->method, return_type, params);
                    
                    free (call_params);
                    free (call_locals);
                    free (target_desc);
                
                } else if (stmt->kind == STMT_RETURN && stmt->arg_kind == ARG_CALL && stmt->target && !target_is_multi_source_class (all_names, nb_sources, stmt->target)) {
                
                    char *target_desc = resolve_class_type (&all_names[i], stmt->target);
                    
                    add_external_method (&external_methods, &nb_external_methods, target_desc, stmt->method, all_names[i].methods[j].return_type, "");
                    free (target_desc);
                
                }
            
            }
            
            for (p = all_names[i].methods[j].params; *p; ) {
            
                const char *start = p;
                char *param_desc;
                unsigned long len;
                
                while (*p == '[') {
                    p++;
                }
                
                if (*p == 'L') {
                
                    while (*p && *p != ';') {
                        p++;
                    }
                    
                    if (*p == ';') {
                        p++;
                    }
                
                } else if (*p) {
                    p++;
                }
                
                len = (unsigned long) (p - start);
                param_desc = xmalloc (len + 1);
                
                memcpy (param_desc, start, len);
                param_desc[len] = '\0';
                
                dex_add_string (&strings, &nb_strings, param_desc);
            
            }
            
            mi++;
        
        }
    
    }
    
    for (i = 0; i < nb_external_methods; i++) {
    
        const char *p;
        
        dex_add_string (&strings, &nb_strings, external_methods[i].class_desc);
        dex_add_string (&strings, &nb_strings, external_methods[i].name);
        dex_add_string (&strings, &nb_strings, external_methods[i].return_type);
        dex_add_string (&strings, &nb_strings, external_methods[i].shorty);
        
        for (p = external_methods[i].params; *p; ) {
        
            const char *start = p;
            char *param_desc;
            unsigned long len;
            
            while (*p == '[') {
                p++;
            }
            
            if (*p == 'L') {
            
                while (*p && *p != ';') {
                    p++;
                }
                
                if (*p == ';') {
                    p++;
                }
            
            } else if (*p) {
                p++;
            }
            
            len = (unsigned long) (p - start);
            param_desc = xmalloc (len + 1);
            
            memcpy (param_desc, start, len);
            param_desc[len] = '\0';
            
            dex_add_string (&strings, &nb_strings, param_desc);
            free (param_desc);
        
        }
    
    }
    
    qsort (strings, nb_strings, sizeof (*strings), dex_string_cmp);
    
    for (i = 0; i < nb_strings; i++) {
        strings[i].index = i;
    }
    
    for (i = 0; i < nb_strings; i++) {
    
        if (strings[i].text[0] == 'L' || strings[i].text[0] == '[' || strlen (strings[i].text) == 1) {
        
            types = xrealloc (types, (nb_types + 1) * sizeof (*types));
            types[nb_types++] = strings[i].text;
        
        }
    
    }
    
    for (i = 0; i < nb_types; i++) {
    
        for (j = i + 1; j < nb_types; j++) {
        
            if (dex_type_order (types[j], types[i], strings, nb_strings) < 0) {
                const char *tmp = types[i]; types[i] = types[j]; types[j] = tmp;
            }
        
        }
    
    }
    
    for (i = 0; i < nb_sources; i++) {
    
        class_type_idx[i] = find_type_idx (types, nb_types, class_descs[i]);
        super_type_idx[i] = super_descs[i] ? find_type_idx (types, nb_types, super_descs[i]) : 0xFFFFFFFFUL;
    
    }
    
    nb_fields = nb_real_fields;
    
    nb_method_ids = nb_real_methods + nb_external_methods;
    nb_protos = nb_real_methods + nb_external_methods;
    
    for (i = 0; i < nb_real_methods; i++) {
    
        struct source_names *names = &all_names[method_map[i].source_index];
        struct method_info *method = &names->methods[method_map[i].method_index];
        
        method_proto_idx[i] = i;
        method_name_idx[i] = dex_find_string (strings, nb_strings, method->name);
        method_id_idx[i] = i;
        method_params_off[i] = 0;
        method_code_off[i] = 0;
    
    }
    
    for (i = 0; i < nb_real_fields; i++) {
    
        struct source_names *names = &all_names[field_map[i].source_index];
        struct field_info *field = &names->fields[field_map[i].field_index];
        
        field_name_idx[i] = dex_find_string (strings, nb_strings, field->name);
        field_type_idx[i] = find_type_idx (types, nb_types, field->type);
        
        field_id_idx[i] = i;
    
    }
    
    for (i = 0; i < nb_external_methods; i++) {
    
        external_methods[i].proto_idx = nb_real_methods + i;
        external_methods[i].name_idx = dex_find_string (strings, nb_strings, external_methods[i].name);
        external_methods[i].method_id_idx = nb_real_methods + i;
        external_methods[i].class_type_idx = find_type_idx (types, nb_types, external_methods[i].class_desc);
        external_methods[i].params_off = 0;
    
    }
    
    string_ids_off = header_size;
    type_ids_off = string_ids_off + nb_strings * 4;
    proto_ids_off = type_ids_off + nb_types * 4;
    field_ids_off = proto_ids_off + nb_protos * 12;
    method_ids_off = field_ids_off + nb_fields * 8;
    class_defs_off = method_ids_off + nb_method_ids * 8;
    data_off = class_defs_off + nb_sources * 32;
    
    while (buf.size < data_off) {
        dex_u1 (&buf, 0);
    }
    
    string_offsets = xmalloc (nb_strings * sizeof (*string_offsets));
    
    for (i = 0; i < nb_strings; i++) {
    
        string_offsets[i] = dex_pos (&buf);
        
        dex_uleb128 (&buf, strlen (strings[i].text));
        dex_bytes (&buf, strings[i].text, strlen (strings[i].text));
        
        dex_u1 (&buf, 0);
    
    }
    
    dex_align4 (&buf);
    
    for (i = 0; i < nb_real_methods; i++) {
    
        struct source_names *names = &all_names[method_map[i].source_index];
        struct method_info *method = &names->methods[method_map[i].method_index];
        const char *p = method->params;
        unsigned long count = count_descriptor_params (p);
        
        if (count) {
        
            if (!type_lists_off) {
                type_lists_off = dex_pos (&buf);
            }
            
            nb_type_lists++;
            
            method_params_off[i] = dex_pos (&buf);
            dex_u4 (&buf, count);
            
            while (*p) {
            
                const char *start = p;
                char *param_desc;
                unsigned long len, ti;
                
                while (*p == '[') {
                    p++;
                }
                
                if (*p == 'L') {
                
                    while (*p && *p != ';') {
                        p++;
                    }
                    
                    if (*p == ';') {
                        p++;
                    }
                
                } else if (*p) {
                    p++;
                }
                
                len = (unsigned long) (p - start);
                param_desc = xmalloc (len + 1);
                
                memcpy (param_desc, start, len);
                param_desc[len] = '\0';
                
                ti = find_type_idx (types, nb_types, param_desc);
                dex_u2 (&buf, ti);
                
                free (param_desc);
            
            }
            
            dex_align4 (&buf);
        
        }
    
    }
    
    for (i = 0; i < nb_external_methods; i++) {
    
        if (count_descriptor_params (external_methods[i].params)) {
        
            if (!type_lists_off) {
                type_lists_off = dex_pos (&buf);
            }
            
            nb_type_lists++;
            write_type_list_for_params (&buf, types, nb_types, external_methods[i].params, &external_methods[i].params_off);
        
        }
    
    }
    
    for (i = 0; i < nb_sources; i++) {
    
        if (all_names[i].nb_interfaces) {
        
            if (!type_lists_off) {
                type_lists_off = dex_pos (&buf);
            }
            
            nb_type_lists++;
            
            interface_type_list_off[i] = dex_pos (&buf);
            dex_u4 (&buf, all_names[i].nb_interfaces);
            
            for (j = 0; j < all_names[i].nb_interfaces; j++) {
                dex_u2 (&buf, find_type_idx (types, nb_types, all_names[i].interfaces[j]));
            }
            
            dex_align4 (&buf);
        
        }
    
    }
    
    for (i = 0; i < nb_real_methods; i++) {
    
        struct source_names *names = &all_names[method_map[i].source_index];
        struct method_info *method = &names->methods[method_map[i].method_index];
        
        unsigned long ins_size = count_descriptor_registers (method->params);
        unsigned long registers_size;
        unsigned long insns_size;
        
        if (!(method->flags & METHOD_STATIC)) {
            ins_size++;
        }
        
        if (method->flags & (METHOD_NATIVE | METHOD_ABSTRACT)) {
            continue;
        }
        
        dex_align4 (&buf);
        
        if (!code_items_off) {
            code_items_off = dex_pos (&buf);
        }
        
        method_code_off[i] = dex_pos (&buf);
        nb_code_items++;
        
        {
        
            int is_constructor = strcmp (method->name, "<init>") == 0;
            int has_return = 0;
            
            unsigned long si;
            registers_size = ins_size + 6 + method_local_slot_count (method);
            
            insns_size = is_constructor ? ((method_has_explicit_constructor_call (method) || !super_descs[method_map[i].source_index]) ? 0 : 3)
                + count_instance_field_initializer_code_units (names) : 0;
            
            for (si = 0; si < method->nb_statements; si++) {
            
                struct statement_info *stmt = &method->statements[si];
                
                if (stmt->kind == STMT_GOTO) {
                    insns_size += 2;
                } else if (stmt->kind == STMT_IF_FALSE) {
                
                    const char *source_type = arithmetic_source_type (names, method, stmt->target);
                    
                    insns_size += arithmetic_source_load_units (stmt->target) + count_arithmetic_code_units (method, source_type, stmt->arg_text)
                        + (stmt->kind == STMT_IF_EQ_CONST ? 4 : 2);
                
                } else if (stmt->kind == STMT_MOVE_EXCEPTION) {
                    insns_size++;
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_STRING_CONCAT) {
                    insns_size += 0;
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_MULTI) {
                    insns_size += 3;
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_RESULT_MULTI) {
                    insns_size += 4 + count_instance_result_call_move_units (method, stmt);
                } else if (statement_is_field_string_call (names, stmt)) {
                    insns_size += 14;
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_RESULT_CALL) {
                    insns_size += 4;
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_NONE) {
                    insns_size += 3;
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_STRING) {
                
                    if (!stmt->target && (method->flags & METHOD_STATIC)) {
                        insns_size += 10;
                    } else {
                        insns_size += 5;
                    }
                
                } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_PARAM) {
                
                    if (!stmt->target && (method->flags & METHOD_STATIC)) {
                        insns_size += 8;
                    } else {
                        insns_size += 3;
                    }
                
                } else if (stmt->kind == STMT_LOCAL) {
                
                    if (stmt->arg_kind == ARG_THIS) {
                        insns_size += 2;
                    } else if (stmt->arg_kind == ARG_NEW) {
                    
                        unsigned long *ctor_locals = 0, nb_ctor_locals = 0, ai;
                        
                        unsigned long local_index = stmt->target[0] == '$' ? strtoul (stmt->target + 1, 0, 10) : 0;
                        unsigned long dst_reg = stmt->target[0] == '$' ? method_local_register (method, local_index) : stmt->target[0] == '#' ? method_param_register (method, registers_size, ins_size, strtoul (stmt->target + 1, 0, 10)) : 0;
                        
                        char *ctor_params = 0;
                        int needs_low_registers = dst_reg >= 16;
                        
                        if (split_constructor_arguments (stmt->arg_text, &ctor_params, &ctor_locals, &nb_ctor_locals)) {
                            return 1;
                        }
                        
                        for (ai = 0; ai < nb_ctor_locals; ai++) {
                        
                            if (method_local_register (method, ctor_locals[ai]) >= 16) {
                                needs_low_registers = 1;
                            }
                        
                        }
                        
                        insns_size += 5 + (needs_low_registers ? 2 * (nb_ctor_locals + 1) : 0);
                        
                        free (ctor_params);
                        free (ctor_locals);
                    
                    } else if (stmt->arg_kind == ARG_NEW_ARRAY) {
                    
                        unsigned long dst_reg = method_local_register (method, strtoul (stmt->target + 1, 0, 10));
                        unsigned long size_reg = method_local_register (method, strtoul (stmt->method + 1, 0, 10));
                        
                        insns_size += dex_move_value_code_units (1, size_reg) + 2 + dex_move_value_code_units (dst_reg, 0);
                    
                    } else if (stmt->arg_kind == ARG_ARRAY_GET) {
                        insns_size += stmt->method[0] == '$' || stmt->method[0] == '#' ? 2 : 4;
                    } else if (stmt->arg_kind == ARG_ARRAY_LENGTH) {
                        insns_size += stmt->method[0] == '$' || stmt->method[0] == '#' ? 1 : 3;
                    } else if (stmt->arg_kind == ARG_STATIC_GET) {
                        insns_size += method_local_register (method, strtoul (stmt->target + 1, 0, 10)) < 256 ? 2 : 5;
                    } else if (stmt->arg_kind == ARG_CHECKCAST) {
                    
                        unsigned long source_local = strtoul (stmt->method + 1, 0, 10);
                        unsigned long result_local = strtoul (stmt->target + 1, 0, 10);
                        unsigned long source_reg = method_local_register (method, source_local);
                        unsigned long result_reg = method_local_register (method, result_local);
                        
                        insns_size += (source_reg < 16 ? 1 : 2) + 2
                            + (result_reg < 16 ? 1 : result_reg < 256 ? 2 : 3);
                    
                    } else if (stmt->arg_kind == ARG_INSTANCEOF) {
                    
                        unsigned long source_local = strtoul (stmt->method + 1, 0, 10);
                        unsigned long result_local = strtoul (stmt->target + 1, 0, 10);
                        unsigned long source_reg = method_local_register (method, source_local);
                        unsigned long result_reg = method_local_register (method, result_local);
                        
                        insns_size += (source_reg < 16 ? 1 : 2) + 2
                            + (result_reg < 16 ? 1 : result_reg < 256 ? 2 : 3);
                    
                    } else if (stmt->arg_kind == ARG_STRING) {
                        insns_size += 2;
                    } else if (stmt->arg_kind == ARG_INT) {
                    
                        const char *target_type = stmt->target[0] == '$' ? method->locals[strtoul (stmt->target + 1, 0, 10)].type : stmt->target[0] == '#' ? method_param_type (method, strtoul (stmt->target + 1, 0, 10)) : find_field_type (names, stmt->target);
                        insns_size += target_type && (target_type[0] == 'J' || target_type[0] == 'D') ? 5 : 3;
                    
                    } else if (stmt->arg_kind == ARG_STRING_CONCAT) {
                        insns_size += 0;
                    } else {
                    
                        const char *source_type = arithmetic_source_type (names, method, stmt->method);
                        insns_size += arithmetic_source_load_units (stmt->method) + count_arithmetic_code_units (method, source_type, stmt->arg_text);
                        
                        if (stmt->target[0] == '$' || stmt->target[0] == '#') {
                            insns_size++;
                        }
                    
                    }
                    
                    if (stmt->target[0] != '$' && stmt->target[0] != '#') {
                        insns_size += 2;
                    }
                
                } else if (stmt->kind == STMT_ARRAY_PUT) {
                    insns_size += 2;
                } else if (stmt->kind == STMT_THROW) {
                
                    insns_size++;
                    has_return = 1;
                
                } else if (stmt->kind == STMT_RETURN) {
                
                    if (stmt->arg_kind == ARG_CALL) {
                        insns_size += 5;
                    } else if (stmt->arg_kind == ARG_STRING) {
                        insns_size += 2;
                    } else if (stmt->arg_kind == ARG_INT) {
                        insns_size += 4;
                    } else if (stmt->arg_kind == ARG_ARITHMETIC) {
                    
                        const char *source_type;
                        
                        if (stmt->target[0] == '#') {
                            source_type = method_param_type (method, strtoul (stmt->target + 1, 0, 10));
                        } else if (stmt->target[0] == '$') {
                            source_type = method->locals[strtoul (stmt->target + 1, 0, 10)].type;
                        } else {
                            source_type = find_field_type (names, stmt->target);
                        }
                        
                        insns_size += 2 + count_arithmetic_code_units (method, source_type, stmt->arg_text);
                        
                        if (stmt->target[0] != '#') {
                            insns_size++;
                        }
                    
                    } else {
                        insns_size++;
                    }
                    
                    has_return = 1;
                
                }
            
            }
            
            if (!has_return) {
            
                if (strcmp (method->return_type, "V") != 0) {
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "missing return value");
                    return 1;
                
                }
                
                insns_size++;
            
            }
            
            dex_write_code_item_header_ex (&buf, registers_size, ins_size, 2, method->nb_tries, insns_size);
            
            {
            
                unsigned long insns_start_pos = dex_pos (&buf);
                
                if (is_constructor && !method_has_explicit_constructor_call (method)) {
                
                    const char *parent_desc = super_descs[method_map[i].source_index];
                    unsigned long this_reg = registers_size - ins_size;
                    
                    if (parent_desc) {
                    
                        unsigned long parent_source = find_source_index_by_descriptor (all_names, nb_sources, parent_desc);
                        unsigned long super_init_idx = METHOD_NOT_FOUND;
                        unsigned long ci;
                        
                        if (parent_source != METHOD_NOT_FOUND) {
                        
                            for (ci = 0; ci < nb_real_methods; ci++) {
                            
                                struct method_info *candidate = &all_names[method_map[ci].source_index].methods[method_map[ci].method_index];
                                
                                if (method_map[ci].source_index == parent_source
                                    && strcmp (candidate->name, "<init>") == 0 && strcmp (candidate->params, "") == 0) {
                                    super_init_idx = method_id_idx[ci];
                                    break;
                                }
                            
                            }
                            
                        }
                        
                        if (super_init_idx == METHOD_NOT_FOUND) {
                        
                            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret,
                                "missing no-argument superclass constructor for '%s'", parent_desc);
                            
                            return 1;
                        
                        }
                        
                        dex_u2 (&buf, 0x1070);
                        dex_u2 (&buf, super_init_idx);
                        dex_u2 (&buf, ((unsigned int) this_reg & 0x0f));
                    
                    }
                    
                    if (dex_write_instance_field_initializers_multi (&buf, all_names, field_map, method_map, nb_real_fields, nb_real_methods, strings, nb_strings, field_id_idx, field_type_idx, method_id_idx, external_methods, nb_external_methods, method_map[i].source_index, this_reg)) {
                        return 1;
                    }
                
                }
                
                for (si = 0; si < method->nb_statements; si++) {
                
                    struct statement_info *stmt = &method->statements[si];
                    stmt->code_offset = (dex_pos (&buf) - insns_start_pos) / 2;
                    
                    if (stmt->kind == STMT_GOTO) {
                    
                        stmt->branch_offset = stmt->code_offset;
                        
                        dex_u2 (&buf, 0x0029);
                        dex_u2 (&buf, 0);
                    
                    } else if (stmt->kind == STMT_IF_FALSE || stmt->kind == STMT_IF_EQ_CONST) {
                    
                        struct field_info *source_field;
                        const char *source_type;
                        
                        unsigned long source_idx;
                        unsigned long source_reg;
                        unsigned long this_reg;
                        unsigned long work_reg;
                        
                        source_type = arithmetic_source_type (names, method, stmt->target);
                        work_reg = 0;
                        
                        if (stmt->target[0] == '#') {
                        
                            source_reg = method_param_register (method, registers_size, ins_size, strtoul (stmt->target + 1, 0, 10));
                            dex_write_move_value (&buf, source_type[0], work_reg, source_reg);
                        
                        } else if (stmt->target[0] == '$') {
                        
                            source_reg = method_local_register (method, strtoul (stmt->target + 1, 0, 10));
                            dex_write_move_value (&buf, source_type[0], work_reg, source_reg);
                        
                        } else {
                        
                            source_field = find_field_info_by_name (names, stmt->target);
                            source_idx = find_field_id_multi (all_names, field_map, nb_real_fields, field_id_idx, method_map[i].source_index, stmt->target);
                            
                            if (source_field->flags & METHOD_STATIC) {
                                dex_u2 (&buf, dex_sget_opcode_for_type (source_type) | ((unsigned int) work_reg << 8));
                            } else {
                            
                                this_reg = registers_size - ins_size;
                                dex_u2 (&buf, dex_iget_opcode_for_type (source_type) | ((unsigned int) work_reg << 8) | ((unsigned int) this_reg << 12));
                            
                            }
                            
                            dex_u2 (&buf, source_idx);
                        
                        }
                        
                        dex_write_mul_factor_list (&buf, method, source_type, "I", work_reg, stmt->arg_text);
                        
                        if (stmt->kind == STMT_IF_EQ_CONST) {
                        
                            dex_u2 (&buf, 0x0113);
                            dex_u2 (&buf, (unsigned int) stmt->arg_param_index & 0xffff);
                            
                            stmt->branch_offset = (dex_pos (&buf) - insns_start_pos) / 2;
                            
                            dex_u2 (&buf, 0x1032 | ((unsigned int) work_reg << 8));
                            dex_u2 (&buf, 0);
                        
                        } else {
                        
                            stmt->branch_offset = (dex_pos (&buf) - insns_start_pos) / 2;
                            
                            dex_u2 (&buf, 0x0038 | ((unsigned int) work_reg << 8));
                            dex_u2 (&buf, 0);
                        
                        }
                    
                    } else if (stmt->kind == STMT_MOVE_EXCEPTION) {
                    
                        unsigned long local_index;
                        unsigned long exception_reg;
                        
                        local_index = strtoul (stmt->target + 1, 0, 10);
                        exception_reg = method_local_register (method, local_index);
                        
                        dex_u2 (&buf, 0x000d | ((exception_reg & 0xff) << 8));
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_STRING_CONCAT) {
                    
                        unsigned long call_idx;
                        
                        if (!stmt->target) {
                        
                            call_idx = find_instance_method_id_multi_from_source (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, method_map[i].source_index, stmt->method);
                            
                            if (call_idx == METHOD_NOT_FOUND) {
                                call_idx = find_inherited_static_method_id_multi_from_source (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, method_map[i].source_index, stmt->method);
                            }
                        
                        } else {
                        
                            call_idx = find_method_id_multi_by_class_name (all_names, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, stmt->target, stmt->method);
                            
                            if (call_idx == METHOD_NOT_FOUND && !target_is_multi_source_class (all_names, nb_sources, stmt->target)) {
                            
                                char *target_desc = resolve_class_type (names, stmt->target);
                                call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, "V", declared_reference_call_parameter (target_desc, stmt->method));
                                
                                free (target_desc);
                            
                            }
                        
                        }
                        
                        if (call_idx == METHOD_ACCESS_DENIED) {
                        
                            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "method '%s' is not accessible from '%s'", stmt->method, names->class_name);
                            return 1;
                        
                        }
                        
                        if (call_idx == METHOD_NOT_FOUND) {
                        
                            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown method '%s'", stmt->method);
                            return 1;
                        
                        }
                        
                        report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret,
                            "runtime string concatenation is not implemented");
                        
                        return 1;
                    
                    } else if (statement_is_field_string_call (names, stmt)) {
                    
                        const char *field_type = find_field_type (names, stmt->target);
                        char *class_name = descriptor_to_simple_class_name (field_type);
                        
                        unsigned long type_idx = find_type_idx (types, nb_types, field_type);
                        unsigned long field_idx = find_field_id_multi (all_names, field_map, nb_real_fields, field_id_idx, method_map[i].source_index, stmt->target);
                        unsigned long init_idx = find_method_id_multi_by_class_name (all_names, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, class_name ? class_name : stmt->target, "<init>");
                        unsigned long call_idx = find_method_id_multi_by_class_name (all_names, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, class_name ? class_name : stmt->target, stmt->method);
                        unsigned long str_idx = dex_find_string (strings, nb_strings, stmt->arg_text);
                        
                        if (init_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_ACCESS_DENIED) {
                        
                            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "private method '%s.%s' is not accessible from '%s'", class_name ? class_name : stmt->target, stmt->method, names->class_name);
                            
                            free (class_name);
                            return 1;
                        
                        }
                        
                        if (field_idx == METHOD_NOT_FOUND || init_idx == METHOD_NOT_FOUND || call_idx == METHOD_NOT_FOUND) {
                        
                            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown field method '%s.%s'", stmt->target, stmt->method);
                            
                            free (class_name);
                            return 1;
                        
                        }
                        
                        dex_u2 (&buf, 0x0022);
                        dex_u2 (&buf, type_idx);
                        dex_u2 (&buf, 0x1070);
                        dex_u2 (&buf, init_idx);
                        dex_u2 (&buf, 0x0000);
                        dex_u2 (&buf, 0x0067);
                        dex_u2 (&buf, field_idx);
                        dex_u2 (&buf, 0x0062);
                        dex_u2 (&buf, field_idx);
                        dex_u2 (&buf, 0x011A);
                        dex_u2 (&buf, str_idx);
                        dex_u2 (&buf, 0x206E);
                        dex_u2 (&buf, call_idx);
                        dex_u2 (&buf, dex_invoke_regs2 (0, 1));
                        
                        free (class_name);
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_MULTI && stmt->target && strcmp (stmt->target, "@this") == 0) {
                    
                        unsigned int regs[5], reg_count = 1;
                        unsigned long this_reg = registers_size - ins_size;
                        unsigned long call_idx;
                        unsigned long *arg_locals = 0, nb_arg_locals = 0, ai;
                        
                        char *params = 0;
                        
                        if (!is_constructor) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "this constructor call is only valid in a constructor");
                            return 1;
                        
                        }
                        
                        if (split_constructor_arguments (stmt->arg_text, &params, &arg_locals, &nb_arg_locals)) {
                            return 1;
                        }
                        
                        call_idx = find_instance_method_id_multi_from_source_params (all_names, nb_sources, method_map, nb_real_methods,
                            method_id_idx, method_map[i].source_index, method_map[i].source_index, "<init>", params);
                        
                        if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND || call_idx == method_id_idx[i]) {
                        
                            char *argument_types = format_argument_types (params);
                            
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret,
                                "no constructor overload matches argument types %s", argument_types);
                            
                            free (argument_types);
                            free (arg_locals);
                            free (params);
                            
                            return 1;
                        
                        }
                        
                        regs[0] = (unsigned int) this_reg;
                        
                        for (ai = 0; ai < nb_arg_locals; ai++) {
                        
                            const char *arg_type = method->locals[arg_locals[ai]].type;
                            unsigned int r = (unsigned int) method_local_register (method, arg_locals[ai]);
                            
                            if (reg_count >= 5 || ((arg_type[0] == 'J' || arg_type[0] == 'D') && reg_count >= 4)) {
                            
                                report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "too many constructor argument registers");
                                
                                free (arg_locals);
                                free (params);
                                
                                return 1;
                            
                            }
                            
                            regs[reg_count++] = r;
                            
                            if (arg_type[0] == 'J' || arg_type[0] == 'D') {
                                regs[reg_count++] = r + 1;
                            }
                        
                        
                        }
                        
                        dex_u2 (&buf, 0x0070 | ((reg_count & 0x0f) << 12) | ((reg_count == 5 ? regs[4] : 0) << 8));
                        dex_u2 (&buf, call_idx);
                        dex_u2 (&buf, dex_invoke_regs4 (regs[0], reg_count > 1 ? regs[1] : 0, reg_count > 2 ? regs[2] : 0, reg_count > 3 ? regs[3] : 0));
                        
                        free (arg_locals);
                        free (params);
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_MULTI && stmt->target && strcmp (stmt->target, "@super") == 0) {
                    
                        unsigned int regs[5], reg_count = 1;
                        unsigned long this_reg = registers_size - ins_size;
                        unsigned long super_source, call_idx;
                        unsigned long *arg_locals = 0, nb_arg_locals = 0, ai;
                        
                        char *params = 0;
                        
                        if (!names->super_class) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "class has no explicit superclass");
                            return 1;
                        
                        }
                        
                        if (strcmp (stmt->method, "<init>") == 0 && !is_constructor) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "super constructor call is only valid in a constructor");
                            return 1;
                        
                        }
                        
                        if (split_constructor_arguments (stmt->arg_text, &params, &arg_locals, &nb_arg_locals)) {
                            return 1;
                        }
                        
                        super_source = find_source_index_by_descriptor (all_names, nb_sources, names->super_class);
                        call_idx = super_source == METHOD_NOT_FOUND
                            ? find_external_method_id (external_methods, nb_external_methods, names->super_class, stmt->method, "V", params)
                                : find_instance_method_id_multi_from_source_params (all_names, nb_sources, method_map, nb_real_methods,
                                    method_id_idx, method_map[i].source_index, super_source, stmt->method, params);
                        
                        if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                        
                            char *argument_types = format_argument_types (params);
                            
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret,
                                "no superclass overload of '%s' matches argument types %s", stmt->method, argument_types);
                            
                            free (argument_types);
                            free (arg_locals);
                            free (params);
                            
                            return 1;
                        
                        }
                        
                        regs[0] = (unsigned int) this_reg;
                        
                        for (ai = 0; ai < nb_arg_locals; ai++) {
                        
                            const char *arg_type = method->locals[arg_locals[ai]].type;
                            unsigned int r = (unsigned int) method_local_register (method, arg_locals[ai]);
                            
                            if (reg_count >= 5 || ((arg_type[0] == 'J' || arg_type[0] == 'D') && reg_count >= 4)) {
                            
                                report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "too many method argument registers");
                                
                                free (arg_locals);
                                free (params);
                                
                                return 1;
                            
                            }
                            
                            regs[reg_count++] = r;
                            
                            if (arg_type[0] == 'J' || arg_type[0] == 'D') {
                                regs[reg_count++] = r + 1;
                            }
                        
                        
                        }
                        
                        dex_u2 (&buf, (strcmp (stmt->method, "<init>") == 0 ? 0x0070 : 0x006f) | ((reg_count & 0x0f) << 12) | ((reg_count == 5 ? regs[4] : 0) << 8));
                        dex_u2 (&buf, call_idx);
                        dex_u2 (&buf, dex_invoke_regs4 (regs[0], reg_count > 1 ? regs[1] : 0, reg_count > 2 ? regs[2] : 0, reg_count > 3 ? regs[3] : 0));
                        
                        free (arg_locals);
                        free (params);
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_MULTI && stmt->target && stmt->target[0] == '$') {
                    
                        unsigned long local_index = strtoul (stmt->target + 1, 0, 10);
                        unsigned long object_reg = method_local_register (method, local_index);
                        
                        const char *target_desc = method->locals[local_index].type;
                        unsigned long target_source = find_source_index_by_descriptor (all_names, nb_sources, target_desc);
                        
                        unsigned int regs[5], reg_count = 1;
                        char *params = 0;
                        
                        unsigned long *arg_locals = 0, nb_arg_locals = 0, ai;
                        unsigned long call_idx;
                        
                        if (split_constructor_arguments (stmt->arg_text, &params, &arg_locals, &nb_arg_locals)) {
                            return 1;
                        }
                        
                        if (target_source != METHOD_NOT_FOUND) {
                            call_idx = find_instance_method_id_multi_from_source_params (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, target_source, stmt->method, params);
                        } else {
                            call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, "V", params);
                        }
                        
                        if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                        
                            char *argument_types = format_argument_types (params);
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "no overload of '%s' matches argument types %s", stmt->method, argument_types);
                            
                            free (argument_types);
                            free (arg_locals);
                            free (params);
                            
                            return 1;
                        
                        }
                        
                        regs[0] = (unsigned int) object_reg;
                        
                        for (ai = 0; ai < nb_arg_locals; ai++) {
                        
                            const char *arg_type = method->locals[arg_locals[ai]].type;
                            unsigned int r = (unsigned int) method_local_register (method, arg_locals[ai]);
                            
                            if (reg_count >= 5 || ((arg_type[0] == 'J' || arg_type[0] == 'D') && reg_count >= 4)) {
                            
                                report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "too many method argument registers");
                                
                                free (arg_locals);
                                free (params);
                                
                                return 1;
                            
                            }
                            
                            regs[reg_count++] = r;
                            
                            if (arg_type[0] == 'J' || arg_type[0] == 'D') {
                                regs[reg_count++] = r + 1;
                            }
                        
                        }

                        dex_u2 (&buf, 0x006e | ((reg_count & 0x0f) << 12) | ((reg_count == 5 ? regs[4] : 0) << 8));
                        dex_u2 (&buf, call_idx);
                        
                        dex_u2 (&buf, dex_invoke_regs4 (regs[0], reg_count > 1 ? regs[1] : 0, reg_count > 2 ? regs[2] : 0, reg_count > 3 ? regs[3] : 0));
                        
                        free (arg_locals);
                        free (params);
                    
                    } else if (stmt->kind == STMT_CALL && (stmt->arg_kind == ARG_MULTI || stmt->arg_kind == ARG_RESULT_MULTI)) {
                    
                        unsigned int regs[5], reg_count = 0;
                        
                        char reg_types[5] = { 0, 0, 0, 0, 0 };
                        char *params = 0;
                        
                        unsigned long *arg_locals = 0, nb_arg_locals = 0, ai;
                        unsigned long call_idx = METHOD_NOT_FOUND;
                        
                        int instance_call = stmt->target && stmt->target[0] == '$';
                        int invoke_static = stmt->target != 0 && !instance_call;
                        
                        if (split_constructor_arguments (stmt->arg_text, &params, &arg_locals, &nb_arg_locals)) {
                            return 1;
                        }
                        
                        if (instance_call) {
                        
                            unsigned long receiver_index = strtoul (stmt->target + 1, 0, 10);
                            const char *target_desc = method->locals[receiver_index].type;
                            
                            unsigned long target_source = find_source_index_by_descriptor (all_names, nb_sources, target_desc);
                            const char *return_type = stmt->arg_kind == ARG_RESULT_MULTI ? method->locals[stmt->arg_param_index].type : "V";
                            
                            if (target_source != METHOD_NOT_FOUND) {
                                call_idx = find_instance_method_id_multi_from_source_params (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, target_source, stmt->method, params);
                            } else {
                                call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, return_type, params);
                            }
                            
                            reg_types[reg_count] = 'L';
                            regs[reg_count++] = (unsigned int) method_local_register (method, receiver_index);
                        
                        } else if (stmt->target) {
                        
                            call_idx = find_static_method_id_multi_params (all_names, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, stmt->target, stmt->method, params);
                            
                            if (call_idx == METHOD_NOT_FOUND && !target_is_multi_source_class (all_names, nb_sources, stmt->target)) {
                            
                                char *target_desc = resolve_class_type (names, stmt->target);
                                const char *return_type = stmt->arg_kind == ARG_RESULT_MULTI ? method->locals[stmt->arg_param_index].type : "V";
                                
                                call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, return_type, params);
                                free (target_desc);
                            
                            }
                        
                        } else {
                        
                            unsigned long this_reg = registers_size - ins_size;
                            call_idx = find_instance_method_id_multi_from_source_params (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, method_map[i].source_index, stmt->method, params);
                            
                            invoke_static = 0;
                            
                            reg_types[reg_count] = 'L';
                            regs[reg_count++] = (unsigned int) this_reg;
                        
                        }
                        
                        if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                        
                            char *argument_types = format_argument_types (params);
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "no overload of '%s' matches argument types %s", stmt->method, argument_types);
                            
                            free (argument_types);
                            free (arg_locals);
                            free (params);
                            
                            return 1;
                        
                        }
                        
                        for (ai = 0; ai < nb_arg_locals; ai++) {
                        
                            const char *arg_type = method->locals[arg_locals[ai]].type;
                            unsigned int r = (unsigned int) method_local_register (method, arg_locals[ai]);
                            
                            if (reg_count >= 5 || ((arg_type[0] == 'J' || arg_type[0] == 'D') && reg_count >= 4)) {
                            
                                report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "too many method argument registers");
                                
                                free (arg_locals);
                                free (params);
                                
                                return 1;
                            
                            }
                            
                            reg_types[reg_count] = arg_type[0];
                            regs[reg_count++] = r;
                            
                            if (arg_type[0] == 'J' || arg_type[0] == 'D') {
                            
                                reg_types[reg_count] = 0;
                                regs[reg_count++] = r + 1;
                            
                            }
                        
                        }
                        
                        {
                        
                            unsigned int ri;
                            int need_low_regs = 0;
                            
                            for (ri = 0; ri < reg_count; ri++) {
                            
                                if (regs[ri] > 15) {
                                
                                    need_low_regs = 1;
                                    break;
                                
                                }
                            
                            }
                            
                            if (need_low_regs) {
                            
                                for (ri = 0; ri < reg_count; ri++) {
                                
                                    if (reg_types[ri] != 0) {
                                        dex_write_move_value (&buf, reg_types[ri], ri, regs[ri]);
                                    }
                                    
                                    regs[ri] = ri;
                                
                                }
                            
                            }
                        
                        }
                        
                        dex_u2 (&buf, (invoke_static ? 0x0071 : 0x006e) | ((reg_count & 0x0f) << 12) | ((reg_count == 5 ? regs[4] : 0) << 8));
                        dex_u2 (&buf, call_idx);
                        
                        dex_u2 (&buf, dex_invoke_regs4 (reg_count > 0 ? regs[0] : 0, reg_count > 1 ? regs[1] : 0, reg_count > 2 ? regs[2] : 0, reg_count > 3 ? regs[3] : 0));
                        
                        if (stmt->arg_kind == ARG_RESULT_MULTI) {
                        
                            unsigned long result_reg = method_local_register (method, stmt->arg_param_index);
                            const char *result_type = method->locals[stmt->arg_param_index].type;
                            
                            unsigned int move_opcode = (result_type[0] == 'J' || result_type[0] == 'D') ? 0x0b : (result_type[0] == 'L' || result_type[0] == '[' ? 0x0c : 0x0a);
                            dex_u2 (&buf, move_opcode | ((unsigned int) result_reg << 8));
                        
                        }
                        
                        free (arg_locals);
                        free (params);
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_NONE && stmt->target && strcmp (stmt->target, "@this") == 0) {
                    
                        unsigned long this_reg = registers_size - ins_size;
                        unsigned long call_idx;
                        
                        if (!is_constructor) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "this constructor call is only valid in a constructor");
                            return 1;
                        
                        }
                        
                        call_idx = find_instance_method_id_multi_from_source (all_names, nb_sources, method_map, nb_real_methods,
                            method_id_idx, method_map[i].source_index, method_map[i].source_index, "<init>");
                        
                        if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND || call_idx == method_id_idx[i]) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "unknown no-argument constructor overload");
                            return 1;
                        
                        }
                        
                        dex_u2 (&buf, 0x1070);
                        dex_u2 (&buf, call_idx);
                        dex_u2 (&buf, ((unsigned int) this_reg & 0x0f));
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_NONE && stmt->target && strcmp (stmt->target, "@super") == 0) {
                    
                        unsigned long super_source, call_idx;
                        unsigned long this_reg = registers_size - ins_size;
                        
                        if (!names->super_class) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "class has no explicit superclass");
                            return 1;
                        
                        }
                        
                        if (strcmp (stmt->method, "<init>") == 0 && !is_constructor) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "super constructor call is only valid in a constructor");
                            return 1;
                        
                        }
                        
                        super_source = find_source_index_by_descriptor (all_names, nb_sources, names->super_class);
                        call_idx = super_source == METHOD_NOT_FOUND
                            ? find_external_method_id (external_methods, nb_external_methods, names->super_class, stmt->method, "V", "")
                            : find_instance_method_id_multi_from_source_params (all_names, nb_sources, method_map, nb_real_methods,
                                method_id_idx, method_map[i].source_index, super_source, stmt->method, "");
                        
                        if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret,
                                "unknown superclass method '%s'", stmt->method);
                            return 1;
                        
                        }
                        
                        dex_u2 (&buf, (strcmp (stmt->method, "<init>") == 0 ? 0x1070 : 0x106f));
                        dex_u2 (&buf, call_idx);
                        dex_u2 (&buf, ((unsigned int) this_reg & 0x0f));
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_NONE && (!stmt->target || stmt->target[0] != '$')) {
                    
                        unsigned long call_idx;
                        
                        if (!stmt->target) {
                        
                            call_idx = find_instance_method_id_multi_from_source (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, method_map[i].source_index, stmt->method);
                            
                            if (call_idx == METHOD_NOT_FOUND) {
                                call_idx = find_inherited_static_method_id_multi_from_source (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, method_map[i].source_index, stmt->method);
                            }
                        
                        } else {
                        
                            if (!class_target_available (all_names, nb_sources, names, stmt->target)) {
                            
                                report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, statement_target_caret (stmt),
                                    "unknown class '%s'", stmt->target);
                                return 1;
                            
                            }
                            
                            call_idx = find_method_id_multi_by_class_name (all_names, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, stmt->target, stmt->method);
                        
                        }
                        
                        if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "unknown method '%s'", stmt->method);
                            return 1;
                        
                        }
                        
                        dex_u2 (&buf, 0x0071);
                        dex_u2 (&buf, call_idx);
                        dex_u2 (&buf, 0);
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_RESULT_CALL && stmt->target && stmt->target[0] != '$') {
                    
                        unsigned long result_index = strtoul (stmt->arg_text + 1, 0, 10);
                        unsigned long result_reg = method_local_register (method, result_index);
                        
                        const char *return_type = method->locals[result_index].type;
                        unsigned long call_idx;
                        
                        if (!class_target_available (all_names, nb_sources, names, stmt->target)) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start,
                                statement_target_caret (stmt), "unknown class '%s'", stmt->target);
                            return 1;
                        
                        }
                        
                        call_idx = find_static_method_id_multi (all_names, method_map, nb_real_methods,
                            method_id_idx, method_map[i].source_index, stmt->target, stmt->method);
                        
                        if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start,
                                stmt->caret, "unknown static method '%s.%s'", stmt->target, stmt->method);
                            return 1;
                        
                        }
                        
                        dex_u2 (&buf, 0x0071);
                        dex_u2 (&buf, call_idx);
                        dex_u2 (&buf, 0);
                        dex_u2 (&buf, dex_move_result_opcode (return_type, result_reg));
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_RESULT_CALL && stmt->target && stmt->target[0] == '$') {
                    
                        unsigned long local_index = strtoul (stmt->target + 1, 0, 10);
                        unsigned long result_index = strtoul (stmt->arg_text + 1, 0, 10);
                        unsigned long object_reg = method_local_register (method, local_index);
                        unsigned long result_reg = method_local_register (method, result_index);
                        
                        const char *target_desc = method->locals[local_index].type;
                        const char *return_type = method->locals[result_index].type;
                        
                        unsigned long target_source = find_source_index_by_descriptor (all_names, nb_sources, target_desc);
                        unsigned long call_idx;
                        
                        if (target_source != METHOD_NOT_FOUND) {
                            call_idx = find_instance_method_id_multi_from_source (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, target_source, stmt->method);
                        } else {
                            call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, return_type, "");
                        }
                        
                        if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "unknown instance method '%s'", stmt->method);
                            return 1;
                        
                        }
                        
                        dex_u2 (&buf, object_reg < 16 ? 0x106E : 0x0174);
                        dex_u2 (&buf, call_idx);
                        
                        dex_u2 (&buf, (unsigned int) object_reg);
                        dex_u2 (&buf, dex_move_result_opcode (return_type, result_reg));
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_NONE && stmt->target && stmt->target[0] == '$') {
                    
                        unsigned long local_index = strtoul (stmt->target + 1, 0, 10);
                        unsigned long object_reg = method_local_register (method, local_index);
                        
                        const char *target_desc = method->locals[local_index].type;
                        
                        unsigned long target_source = find_source_index_by_descriptor (all_names, nb_sources, target_desc);
                        unsigned long call_idx;
                        
                        if (target_source != METHOD_NOT_FOUND) {
                            call_idx = find_instance_method_id_multi_from_source (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, target_source, stmt->method);
                        } else {
                            call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, "V", "");
                        }
                        
                        if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "unknown instance method '%s'", stmt->method);
                            return 1;
                        
                        }
                        
                        if (object_reg < 16) {
                        
                            dex_u2 (&buf, 0x106E);
                            dex_u2 (&buf, call_idx);
                            dex_u2 (&buf, (unsigned int) object_reg);
                        
                        } else {
                        
                            dex_u2 (&buf, 0x0174);
                            dex_u2 (&buf, call_idx);
                            dex_u2 (&buf, (unsigned int) object_reg);
                        
                        }
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_STRING && stmt->target && stmt->target[0] == '$') {

                        unsigned long local_index = strtoul (stmt->target + 1, 0, 10);
                        unsigned long object_reg = method_local_register (method, local_index);
                        unsigned long str_idx = dex_find_string (strings, nb_strings, stmt->arg_text);
                        
                        const char *target_desc = method->locals[local_index].type;
                        
                        unsigned long target_source = find_source_index_by_descriptor (all_names, nb_sources, target_desc);
                        unsigned long call_idx;
                        
                        if (target_source != METHOD_NOT_FOUND) {
                            call_idx = find_instance_method_id_multi_from_source_params (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, target_source, stmt->method, declared_reference_call_parameter (target_desc, stmt->method));
                        } else {
                            call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, "V", declared_reference_call_parameter (target_desc, stmt->method));
                        }
                        
                        if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "no overload of '%s' matches the reference argument type", stmt->method);
                            return 1;
                        
                        }
                        
                        dex_u2 (&buf, 0x001A);
                        dex_u2 (&buf, str_idx);
                        
                        dex_u2 (&buf, 0x206E);
                        dex_u2 (&buf, call_idx);
                        
                        dex_u2 (&buf, dex_invoke_regs2 ((unsigned int) object_reg, 0));
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_STRING && !find_field_type (names, stmt->target)) {
                    
                        unsigned long str_idx = dex_find_string (strings, nb_strings, stmt->arg_text);
                        unsigned long call_idx;
                        
                        if (!stmt->target) {
                        
                            call_idx = find_instance_method_id_multi_from_source (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, method_map[i].source_index, stmt->method);
                            
                            if (call_idx == METHOD_ACCESS_DENIED) {
                            
                                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "method '%s' is not accessible from '%s'", stmt->method, names->class_name);
                                return 1;
                            
                            }
                            
                            if (call_idx == METHOD_NOT_FOUND) {
                                call_idx = find_inherited_static_method_id_multi_from_source (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, method_map[i].source_index, stmt->method);
                            }
                            
                            if (call_idx == METHOD_ACCESS_DENIED) {
                            
                                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "method '%s' is not accessible from '%s'", stmt->method, names->class_name);
                                return 1;
                            
                            }
                            
                            if (call_idx == METHOD_NOT_FOUND) {
                            
                                if (missing_implemented_abstract_method_multi (all_names, nb_sources, method_map[i].source_index, stmt->method, stmt->arg_kind == ARG_STRING ? declared_reference_call_parameter (class_descs[method_map[i].source_index], stmt->method) : 0)) {
                                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "class '%s' does not implement abstract method '%s'", names->class_name, stmt->method);
                                } else {
                                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown inherited method '%s'", stmt->method);
                                }
                                
                                return 1;
                            
                            }
                            
                            if (find_inherited_static_method_id_multi_from_source (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, method_map[i].source_index, stmt->method) == call_idx) {
                            
                                dex_u2 (&buf, 0x011A);
                                dex_u2 (&buf, str_idx);
                                dex_u2 (&buf, 0x1071);
                                dex_u2 (&buf, call_idx);
                                dex_u2 (&buf, 0x0000);
                            
                            } else if (method->flags & METHOD_STATIC) {
                            
                                unsigned long init_idx = find_method_id_multi_by_class_name (all_names, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, names->class_name, "<init>");
                                
                                if (init_idx == METHOD_ACCESS_DENIED) {
                                
                                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "constructor '%s.<init>' is not accessible", names->class_name);
                                    return 1;
                                
                                }
                                
                                if (init_idx == METHOD_NOT_FOUND) {
                                
                                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "missing constructor '%s.<init>'", names->class_name);
                                    return 1;
                                
                                }
                                
                                dex_u2 (&buf, 0x0022);
                                dex_u2 (&buf, class_type_idx[method_map[i].source_index]);
                                dex_u2 (&buf, 0x1070);
                                dex_u2 (&buf, init_idx);
                                dex_u2 (&buf, 0x0000);
                                dex_u2 (&buf, 0x011A);
                                dex_u2 (&buf, str_idx);
                                dex_u2 (&buf, 0x206E);
                                dex_u2 (&buf, call_idx);
                                dex_u2 (&buf, dex_invoke_regs2 (0, 1));
                            
                            } else {
                            
                                unsigned long this_reg = registers_size - ins_size;
                                
                                dex_u2 (&buf, 0x001A);
                                dex_u2 (&buf, str_idx);
                                dex_u2 (&buf, 0x206E);
                                dex_u2 (&buf, call_idx);
                                dex_u2 (&buf, dex_invoke_regs2 ((unsigned int) this_reg, 0));
                            
                            }
                        
                        } else {
                        
                            call_idx = find_static_method_id_multi (all_names, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, stmt->target, stmt->method);
                            
                            if (call_idx == METHOD_NOT_FOUND) {
                            
                                char *target_desc = resolve_class_type (names, stmt->target);
                                call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, "V", declared_reference_call_parameter (target_desc, stmt->method));
                                
                                free (target_desc);
                            
                            }
                            
                            if (call_idx == METHOD_ACCESS_DENIED) {
                            
                                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "private method '%s.%s' is not accessible from '%s'", stmt->target ? stmt->target : names->class_name, stmt->method, names->class_name);
                                return 1;
                            
                            }
                            
                            if (call_idx == METHOD_NOT_FOUND) {
                            
                                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown static method '%s.%s'", stmt->target ? stmt->target : names->class_name, stmt->method);
                                return 1;
                            
                            }
                            
                            dex_u2 (&buf, 0x001A);
                            dex_u2 (&buf, str_idx);
                            dex_u2 (&buf, 0x1071);
                            dex_u2 (&buf, call_idx);
                            dex_u2 (&buf, 0x0000);
                        
                        }
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_PARAM && stmt->target && stmt->target[0] == '$') {
                    
                        unsigned long local_index = strtoul (stmt->target + 1, 0, 10);
                        unsigned long object_reg = method_local_register (method, local_index);
                        unsigned long arg_reg = method_param_register (method, registers_size, ins_size, stmt->arg_param_index);
                        
                        const char *arg_type = method_param_type (method, stmt->arg_param_index);
                        const char *target_desc = method->locals[local_index].type;
                        
                        unsigned long target_source = find_source_index_by_descriptor (all_names, nb_sources, target_desc);
                        unsigned long call_idx;
                        
                        if (target_source != METHOD_NOT_FOUND) {
                            call_idx = find_instance_method_id_multi_from_source_params (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, target_source, stmt->method, arg_type);
                        } else {
                            call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, "V", arg_type);
                        }
                        
                        if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                        
                            char *argument_types = format_argument_types (arg_type);
                            
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "no overload of '%s' matches argument types %s", stmt->method, argument_types);
                            free (argument_types);
                            
                            return 1;
                        
                        }
                        
                        dex_u2 (&buf, 0x206E);
                        dex_u2 (&buf, call_idx);
                        
                        dex_u2 (&buf, dex_invoke_regs2 ((unsigned int) object_reg, (unsigned int) arg_reg));
                    
                    } else if (stmt->kind == STMT_CALL && stmt->arg_kind == ARG_PARAM && !find_field_type (names, stmt->target)) {
                    
                        unsigned long arg_reg = method_param_register (method, registers_size, ins_size, stmt->arg_param_index);
                        unsigned long call_idx;
                        
                        if (!stmt->target) {
                        
                            call_idx = find_instance_method_id_multi_from_source (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, method_map[i].source_index, stmt->method);
                            
                            if (call_idx == METHOD_ACCESS_DENIED) {
                            
                                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "method '%s' is not accessible from '%s'", stmt->method, names->class_name);
                                return 1;
                            
                            }
                            
                            if (call_idx == METHOD_NOT_FOUND) {
                                call_idx = find_inherited_static_method_id_multi_from_source (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, method_map[i].source_index, stmt->method);
                            }
                            
                            if (call_idx == METHOD_ACCESS_DENIED) {
                            
                                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "method '%s' is not accessible from '%s'", stmt->method, names->class_name);
                                return 1;
                            
                            }
                            
                            if (call_idx == METHOD_NOT_FOUND) {
                            
                                if (missing_implemented_abstract_method_multi (all_names, nb_sources, method_map[i].source_index, stmt->method, stmt->arg_kind == ARG_STRING ? declared_reference_call_parameter (class_descs[method_map[i].source_index], stmt->method) : 0)) {
                                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "class '%s' does not implement abstract method '%s'", names->class_name, stmt->method);
                                } else {
                                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown inherited method '%s'", stmt->method);
                                }
                                
                                return 1;
                            
                            }
                            
                            if (find_inherited_static_method_id_multi_from_source (all_names, nb_sources, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, method_map[i].source_index, stmt->method) == call_idx) {
                            
                                dex_u2 (&buf, 0x1071);
                                dex_u2 (&buf, call_idx);
                                dex_u2 (&buf, ((unsigned int) arg_reg & 0x0F));
                            
                            } else if (method->flags & METHOD_STATIC) {
                            
                                unsigned long init_idx = find_method_id_multi_by_class_name (all_names, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, names->class_name, "<init>");
                                
                                if (init_idx == METHOD_ACCESS_DENIED) {
                                
                                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "constructor '%s.<init>' is not accessible", names->class_name);
                                    return 1;
                                
                                }
                                
                                if (init_idx == METHOD_NOT_FOUND) {
                                
                                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "missing constructor '%s.<init>'", names->class_name);
                                    return 1;
                                
                                }
                                
                                dex_u2 (&buf, 0x0022);
                                dex_u2 (&buf, class_type_idx[method_map[i].source_index]);
                                dex_u2 (&buf, 0x1070);
                                dex_u2 (&buf, init_idx);
                                dex_u2 (&buf, 0x0000);
                                dex_u2 (&buf, 0x206E);
                                dex_u2 (&buf, call_idx);
                                dex_u2 (&buf, dex_invoke_regs2 (0, (unsigned int) arg_reg));
                            
                            } else {
                            
                                unsigned long this_reg = registers_size - ins_size;
                                
                                dex_u2 (&buf, 0x206E);
                                dex_u2 (&buf, call_idx);
                                dex_u2 (&buf, dex_invoke_regs2 ((unsigned int) this_reg, (unsigned int) arg_reg));
                            
                            }
                        
                        } else {
                        
                            call_idx = find_static_method_id_multi (all_names, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, stmt->target, stmt->method);
                            
                            if (call_idx == METHOD_NOT_FOUND) {
                            
                                char *target_desc = resolve_class_type (names, stmt->target);
                                call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, "V", "");
                                
                                free (target_desc);
                            
                            }
                            
                            if (call_idx == METHOD_ACCESS_DENIED) {
                            
                                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "private method '%s.%s' is not accessible from '%s'", stmt->target, stmt->method, names->class_name);
                                return 1;
                            
                            }
                            
                            if (call_idx == METHOD_NOT_FOUND) {
                            
                                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown static method '%s.%s'", stmt->target, stmt->method);
                                return 1;
                            
                            }
                            
                            dex_u2 (&buf, 0x1071);
                            dex_u2 (&buf, call_idx);
                            dex_u2 (&buf, ((unsigned int) arg_reg & 0x0F));
                        
                        }
                    
                    } else if (stmt->kind == STMT_LOCAL) {
                    
                        int parameter_assignment = stmt->target[0] == '#';
                        int field_assignment = stmt->target[0] != '$' && !parameter_assignment;
                        
                        unsigned long local_index = field_assignment || parameter_assignment ? 0 : strtoul (stmt->target + 1, 0, 10);
                        unsigned long dst_reg = field_assignment ? 0 : parameter_assignment ? method_param_register (method, registers_size, ins_size, strtoul (stmt->target + 1, 0, 10)) : method_local_register (method, local_index);
                        
                        const char *local_type = field_assignment ? find_field_info_by_name (names, stmt->target)->type : parameter_assignment ? method_param_type (method, strtoul (stmt->target + 1, 0, 10)) : method->locals[local_index].type;
                        
                        if (stmt->arg_kind == ARG_THIS) {
                        
                            unsigned long this_reg = registers_size - ins_size;
                            dex_write_move_value (&buf, 'L', dst_reg, this_reg);
                        
                        } else if (stmt->arg_kind == ARG_STATIC_GET) {
                        
                            unsigned long source_index = find_source_index_by_descriptor (all_names, nb_sources, stmt->method);
                            unsigned long field_idx = source_index == METHOD_NOT_FOUND ? METHOD_NOT_FOUND : find_field_id_multi (all_names, field_map, nb_real_fields, field_id_idx, source_index, stmt->arg_text);
                            
                            if (field_idx == METHOD_NOT_FOUND) {
                            
                                report_line_at (stmt->filename ? stmt->filename : get_filename (),
                                    stmt->line ? stmt->line : get_line_number (), REPORT_ERROR,
                                        stmt->start ? stmt->start : tok.start,
                                            stmt->caret ? stmt->caret : tok.caret,
                                                "unknown static field '%s.%s'", stmt->method, stmt->arg_text);
                                return 1;
                            
                            }
                            
                            if (dst_reg < 256) {
                            
                                dex_u2 (&buf, dex_sget_opcode_for_type (local_type) | ((unsigned int) dst_reg << 8));
                                dex_u2 (&buf, field_idx);
                            
                            } else {
                            
                                dex_u2 (&buf, dex_sget_opcode_for_type (local_type));
                                dex_u2 (&buf, field_idx);
                                
                                dex_write_move_value (&buf, local_type[0], dst_reg, 0);
                            
                            }
                        
                        } else if (stmt->arg_kind == ARG_CHECKCAST) {
                        
                            unsigned long source_local = strtoul (stmt->method + 1, 0, 10);
                            unsigned long source_reg = method_local_register (method, source_local);
                            unsigned long type_idx = find_type_idx (types, nb_types, stmt->arg_text);
                            
                            if (type_idx == METHOD_NOT_FOUND) {
                            
                                report_line_at (stmt->filename, stmt->line, REPORT_ERROR,
                                    stmt->start, stmt->caret, "missing DEX type for reference cast");
                                return 1;
                            
                            }
                            
                            dex_write_move_value (&buf, 'L', 0, source_reg);
                            
                            dex_u2 (&buf, 0x001F);
                            dex_u2 (&buf, type_idx);
                            
                            dex_write_move_value (&buf, 'L', dst_reg, 0);
                        
                        } else if (stmt->arg_kind == ARG_INSTANCEOF) {
                        
                            unsigned long source_local = strtoul (stmt->method + 1, 0, 10);
                            unsigned long source_reg = method_local_register (method, source_local);
                            unsigned long type_idx = find_type_idx (types, nb_types, stmt->arg_text);
                            
                            if (type_idx == METHOD_NOT_FOUND) {
                            
                                report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                                    tok.start, tok.caret, "missing DEX type for instanceof");
                                return 1;
                            
                            }
                            
                            dex_write_move_value (&buf, 'L', 0, source_reg);
                            
                            dex_u2 (&buf, 0x0020);
                            dex_u2 (&buf, type_idx);
                            
                            dex_write_move_value (&buf, 'Z', dst_reg, 0);
                        
                        } else if (stmt->arg_kind == ARG_NEW_ARRAY) {
                        
                            unsigned long size_local = strtoul (stmt->method + 1, 0, 10);
                            unsigned long size_reg = method_local_register (method, size_local);
                            unsigned long type_idx = find_type_idx (types, nb_types, local_type);
                            
                            dex_write_move_value (&buf, 'I', 1, size_reg);
                            
                            dex_u2 (&buf, 0x1023);
                            dex_u2 (&buf, type_idx);
                            
                            dex_write_move_value (&buf, '[', dst_reg, 0);
                        
                        } else if (stmt->arg_kind == ARG_ARRAY_GET) {
                        
                            unsigned long index_local = strtoul (stmt->arg_text + 1, 0, 10);
                            unsigned long index_reg = method_local_register (method, index_local);
                            
                            unsigned long array_reg;
                            
                            if (stmt->method[0] == '$') {
                            
                                unsigned long array_local = strtoul (stmt->method + 1, 0, 10);
                                array_reg = method_local_register (method, array_local);
                            
                            } else if (stmt->method[0] == '#') {
                            
                                unsigned long param_index = strtoul (stmt->method + 1, 0, 10);
                                array_reg = method_param_register (method, registers_size, ins_size, param_index);
                            
                            } else {
                            
                                struct field_info *array_field = find_field_info_by_name (names, stmt->method);
                                unsigned long array_idx = find_field_id_multi (all_names, field_map, nb_real_fields, field_id_idx, method_map[i].source_index, stmt->method);
                                
                                array_reg = dst_reg;
                                
                                if (array_field->flags & METHOD_STATIC) {
                                    dex_u2 (&buf, dex_sget_opcode_for_type (array_field->type) | ((unsigned int) array_reg << 8));
                                } else {
                                
                                    unsigned long this_reg = registers_size - ins_size;
                                    dex_u2 (&buf, dex_iget_opcode_for_type (array_field->type) | ((unsigned int) array_reg << 8) | ((unsigned int) this_reg << 12));
                                
                                }
                                
                                dex_u2 (&buf, array_idx);
                            
                            }
                            
                            dex_u2 (&buf, dex_aget_opcode_for_type (local_type) | ((unsigned int) dst_reg << 8));
                            dex_u2 (&buf, (unsigned int) array_reg | ((unsigned int) index_reg << 8));
                        
                        } else if (stmt->arg_kind == ARG_ARRAY_LENGTH) {
                        
                            unsigned long array_reg;
                            
                            if (stmt->method[0] == '$') {
                            
                                unsigned long array_local = strtoul (stmt->method + 1, 0, 10);
                                array_reg = method_local_register (method, array_local);
                            
                            } else if (stmt->method[0] == '#') {
                            
                                unsigned long param_index = strtoul (stmt->method + 1, 0, 10);
                                array_reg = method_param_register (method, registers_size, ins_size, param_index);
                            
                            } else {
                            
                                struct field_info *array_field = find_field_info_by_name (names, stmt->method);
                                unsigned long array_idx = find_field_id_multi (all_names, field_map, nb_real_fields, field_id_idx, method_map[i].source_index, stmt->method);
                                
                                array_reg = dst_reg;
                                
                                if (array_field->flags & METHOD_STATIC) {
                                    dex_u2 (&buf, dex_sget_opcode_for_type (array_field->type) | ((unsigned int) array_reg << 8));
                                } else {
                                
                                    unsigned long this_reg = registers_size - ins_size;
                                    dex_u2 (&buf, dex_iget_opcode_for_type (array_field->type) | ((unsigned int) array_reg << 8) | ((unsigned int) this_reg << 12));
                                
                                }
                                
                                dex_u2 (&buf, array_idx);
                            
                            }
                            
                            if (array_reg < 16 && dst_reg < 16) {
                                dex_u2 (&buf, 0x0021 | ((unsigned int) dst_reg << 8) | ((unsigned int) array_reg << 12));
                            } else {
                            
                                unsigned long array_length_reg = dst_reg < 16 ? dst_reg : 1;
                                unsigned long array_source_reg = array_reg;
                                
                                if (array_source_reg >= 16) {
                                
                                    dex_u2 (&buf, 0x0008);
                                    dex_u2 (&buf, (unsigned int) array_source_reg);
                                    
                                    array_source_reg = 0;
                                
                                }
                                
                                dex_u2 (&buf, 0x0021 | ((unsigned int) array_length_reg << 8) | ((unsigned int) array_source_reg << 12));
                                
                                if (dst_reg >= 16) {
                                
                                    dex_u2 (&buf, 0x0002 | ((unsigned int) dst_reg << 8));
                                    dex_u2 (&buf, (unsigned int) array_length_reg);
                                
                                }
                            
                            }
                        
                        } else if (stmt->arg_kind == ARG_NEW) {
                        
                            const char *new_type = local_type;
                            
                            char *class_name = descriptor_to_simple_class_name (new_type);
                            unsigned long type_idx = find_type_idx (types, nb_types, new_type);
                            
                            char *ctor_params = 0;
                            unsigned long *ctor_locals = 0, nb_ctor_locals = 0;
                            
                            unsigned int regs[5];
                            unsigned int reg_count = 1;
                            
                            unsigned long ai;
                            unsigned long init_idx;
                            
                            if (split_constructor_arguments (stmt->arg_text, &ctor_params, &ctor_locals, &nb_ctor_locals)) {
                                return 1;
                            }
                            
                            init_idx = find_method_id_multi_by_class_name_params (all_names,
                                method_map, nb_real_methods, method_id_idx, method_map[i].source_index,
                                    class_name ? class_name : stmt->method, "<init>", ctor_params);
                            
                            if (init_idx == METHOD_ACCESS_DENIED || init_idx == METHOD_NOT_FOUND) {
                            
                                char *argument_types = format_argument_types (ctor_params);
                                
                                report_line_at (stmt->filename ? stmt->filename : get_filename (),
                                    stmt->line ? stmt->line : get_line_number (), REPORT_ERROR,
                                        stmt->start ? stmt->start : tok.start,
                                            stmt->caret ? stmt->caret : tok.caret,
                                                "no overload of '%s' matches argument types %s", stmt->method, argument_types);
                                
                                free (argument_types);
                                free (ctor_params);
                                free (ctor_locals);
                                free (class_name);
                                
                                return 1;
                            
                            }
                            
                            if (type_idx == METHOD_NOT_FOUND) {
                            
                                report_line_at (stmt->filename ? stmt->filename : get_filename (),
                                    stmt->line ? stmt->line : get_line_number (), REPORT_ERROR,
                                        stmt->start ? stmt->start : tok.start,
                                            stmt->caret ? stmt->caret : tok.caret,
                                                "missing DEX type for object '%s'", stmt->method);
                                
                                free (class_name);
                                return 1;
                            
                            }
                            
                            regs[0] = (unsigned int) dst_reg;
                            
                            for (ai = 0; ai < nb_ctor_locals; ai++) {
                            
                                const char *arg_type = method->locals[ctor_locals[ai]].type;
                                unsigned int r = (unsigned int) method_local_register (method, ctor_locals[ai]);
                                
                                if (reg_count >= 5 || ((arg_type[0] == 'J' || arg_type[0] == 'D') && reg_count >= 4)) {
                                
                                    report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret, "too many constructor argument registers");
                                    
                                    free (ctor_params);
                                    free (ctor_locals);
                                    
                                    free (class_name);
                                    return 1;
                                
                                }
                                
                                regs[reg_count++] = r;
                                
                                if (arg_type[0] == 'J' || arg_type[0] == 'D') {
                                    regs[reg_count++] = r + 1;
                                }
                            
                            }
                            
                            dex_u2 (&buf, 0x0022 | ((unsigned int) dst_reg << 8));
                            dex_u2 (&buf, type_idx);
                            
                            {
                            
                                int needs_low_registers = 0;
                                unsigned int ri;
                                
                                for (ri = 0; ri < reg_count; ri++) {
                                
                                    if (regs[ri] >= 16) {
                                    
                                        needs_low_registers = 1;
                                        break;
                                    
                                    }
                                
                                }
                                
                                if (needs_low_registers) {
                                
                                    unsigned int low_reg = 0;
                                    
                                    dex_u2 (&buf, 0x0008 | (low_reg << 8));
                                    dex_u2 (&buf, regs[0]);
                                    
                                    low_reg++;
                                    
                                    for (ri = 0; ri < nb_ctor_locals; ri++) {
                                    
                                        const char *arg_type = method->locals[ctor_locals[ri]].type;
                                        unsigned int opcode = 0x0002;
                                        unsigned int source_reg = (unsigned int) method_local_register (method, ctor_locals[ri]);
                                        
                                        if (arg_type[0] == 'L' || arg_type[0] == '[') {
                                            opcode = 0x0008;
                                        } else if (arg_type[0] == 'J' || arg_type[0] == 'D') {
                                            opcode = 0x0005;
                                        }
                                        
                                        dex_u2 (&buf, opcode | (low_reg << 8));
                                        dex_u2 (&buf, source_reg);
                                        
                                        low_reg += arg_type[0] == 'J' || arg_type[0] == 'D' ? 2 : 1;
                                    
                                    }
                                    
                                    for (ri = 0; ri < reg_count; ri++) {
                                        regs[ri] = ri;
                                    }
                                
                                }
                            
                            }
                            
                            dex_u2 (&buf, 0x0070 | ((reg_count & 0x0f) << 12) | ((reg_count == 5 ? regs[4] : 0) << 8));
                            dex_u2 (&buf, init_idx);
                            
                            dex_u2 (&buf, dex_invoke_regs4 (regs[0], reg_count > 1 ? regs[1] : 0, reg_count > 2 ? regs[2] : 0, reg_count > 3 ? regs[3] : 0));
                            
                            free (ctor_params);
                            free (ctor_locals);
                            free (class_name);
                        
                        } else if (stmt->arg_kind == ARG_STRING) {
                        
                            unsigned long str_idx = dex_find_string (strings, nb_strings, stmt->arg_text);
                            dex_u2 (&buf, 0x001a | ((unsigned int) dst_reg << 8));
                            dex_u2 (&buf, str_idx);
                        
                        } else if (stmt->arg_kind == ARG_INT) {
                            dex_write_const_integer (&buf, local_type, dst_reg, stmt->arg_text);
                        } else if (stmt->arg_kind == ARG_STRING_CONCAT) {
                        
                            report_line_at (stmt->filename, stmt->line, REPORT_ERROR, stmt->start, stmt->caret,
                                "runtime string concatenation is not implemented");
                        
                        } else {
                        
                            const char *source_type = arithmetic_source_type (names, method, stmt->method);
                            unsigned long work_reg = 0;
                            
                            if (field_initializer_is_call (stmt->method)) {
                            
                                char *target = 0;
                                char *called_method = 0;
                                unsigned long call_idx;
                                
                                if (split_field_call_initializer (stmt->method, &target, &called_method)) {
                                    return 1;
                                }
                                
                                call_idx = find_static_method_id_multi (all_names, method_map,
                                    nb_real_methods, method_id_idx, method_map[i].source_index,
                                        target, called_method);
                                
                                if (call_idx == METHOD_NOT_FOUND) {
                                
                                    char *target_desc = resolve_class_type (names, target);
                                    call_idx = find_external_method_id (external_methods,
                                        nb_external_methods, target_desc, called_method, "D", "");
                                    free (target_desc);
                                
                                }
                                
                                if (call_idx == METHOD_ACCESS_DENIED || call_idx == METHOD_NOT_FOUND) {
                                
                                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR,
                                        tok.start, tok.caret, "unknown static method '%s.%s'",
                                        target, called_method);
                                    
                                    free (target);
                                    free (called_method);
                                    
                                    return 1;
                                
                                }
                                
                                dex_u2 (&buf, 0x0071);
                                dex_u2 (&buf, call_idx);
                                dex_u2 (&buf, 0x0000);
                                dex_u2 (&buf, dex_move_result_opcode ("D", work_reg));
                                
                                free (target);
                                free (called_method);
                            
                            } else if (stmt->method[0] == '#') {
                            
                                unsigned long param_index = strtoul (stmt->method + 1, 0, 10);
                                unsigned long source_reg = method_param_register (method, registers_size, ins_size, param_index);
                                dex_write_move_value (&buf, source_type[0], work_reg, source_reg);
                            
                            } else if (stmt->method[0] == '$') {
                            
                                unsigned long source_reg = method_local_register (method, strtoul (stmt->method + 1, 0, 10));
                                dex_write_move_value (&buf, source_type[0], work_reg, source_reg);
                            
                            } else {
                            
                                struct field_info *source_field = find_field_info_by_name (names, stmt->method);
                                unsigned long source_idx = find_field_id_multi (all_names, field_map, nb_real_fields, field_id_idx, method_map[i].source_index, stmt->method);
                                
                                if (source_field->flags & METHOD_STATIC) {
                                    dex_u2 (&buf, dex_sget_opcode_for_type (source_type) | ((unsigned int) work_reg << 8));
                                } else {
                                
                                    unsigned long this_reg = registers_size - ins_size;
                                    dex_u2 (&buf, dex_iget_opcode_for_type (source_type) | ((unsigned int) work_reg << 8) | ((unsigned int) this_reg << 12));
                                
                                }
                                
                                dex_u2 (&buf, source_idx);
                            
                            }
                            
                            dex_write_mul_factor_list (&buf, method, source_type, local_type, work_reg, stmt->arg_text);
                            
                            if (!field_assignment && dst_reg != work_reg) {
                                dex_write_move_value (&buf, local_type[0], dst_reg, work_reg);
                            }
                        
                        }
                        
                        if (field_assignment) {
                        
                            struct field_info *dest_field = find_field_info_by_name (names, stmt->target);
                            unsigned long dest_idx = find_field_id_multi (all_names, field_map, nb_real_fields, field_id_idx, method_map[i].source_index, stmt->target);
                            
                            if (dest_field->flags & METHOD_STATIC) {
                                dex_u2 (&buf, dex_sput_opcode_for_type (dest_field->type) | ((unsigned int) dst_reg << 8));
                            } else {
                            
                                unsigned long this_reg = registers_size - ins_size;
                                dex_u2 (&buf, dex_iput_opcode_for_type (dest_field->type) | ((unsigned int) dst_reg << 8) | ((unsigned int) this_reg << 12));
                            
                            }
                            
                            dex_u2 (&buf, dest_idx);
                        
                        }
                    
                    } else if (stmt->kind == STMT_ARRAY_PUT) {
                    
                        unsigned long array_local = strtoul (stmt->target + 1, 0, 10);
                        unsigned long index_local = strtoul (stmt->method + 1, 0, 10);
                        unsigned long value_local = strtoul (stmt->arg_text + 1, 0, 10);
                        
                        unsigned long array_reg = method_local_register (method, array_local);
                        unsigned long index_reg = method_local_register (method, index_local);
                        unsigned long value_reg = method_local_register (method, value_local);
                        
                        const char *element_type = method->locals[array_local].type + 1;
                        
                        dex_u2 (&buf, dex_aput_opcode_for_type (element_type) | ((unsigned int) value_reg << 8));
                        dex_u2 (&buf, (unsigned int) array_reg | ((unsigned int) index_reg << 8));
                    
                    } else if (stmt->kind == STMT_THROW) {
                    
                        unsigned long throw_reg;
                        
                        if (stmt->target[0] == '$') {
                            throw_reg = method_local_register (method, strtoul (stmt->target + 1, 0, 10));
                        } else {
                            throw_reg = method_param_register (method, registers_size, ins_size, strtoul (stmt->target + 1, 0, 10));
                        }
                        
                        dex_u2 (&buf, 0x0027 | ((unsigned int) throw_reg << 8));
                    
                    } else if (stmt->kind == STMT_RETURN) {
                    
                        if (strcmp (method->return_type, "V") == 0) {
                            dex_u2 (&buf, 0x000E);
                        } else if (stmt->arg_kind == ARG_THIS) {
                        
                            unsigned long this_reg = registers_size - ins_size;
                            dex_u2 (&buf, dex_return_opcode (method->return_type, this_reg));
                        
                        } else if (stmt->arg_kind == ARG_CALL) {
                        
                            unsigned long call_idx = stmt->target ? find_static_method_id_multi (all_names, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, stmt->target, stmt->method)
                                : find_static_method_id_multi (all_names, method_map, nb_real_methods, method_id_idx, method_map[i].source_index, names->class_name, stmt->method);
                            
                            if (call_idx == METHOD_NOT_FOUND && stmt->target) {
                            
                                char *target_desc = resolve_class_type (names, stmt->target);
                                call_idx = find_external_method_id (external_methods, nb_external_methods, target_desc, stmt->method, method->return_type, "");
                                
                                free (target_desc);
                            
                            }
                            
                            if (call_idx == METHOD_ACCESS_DENIED) {
                            
                                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "private method '%s.%s' is not accessible from '%s'", stmt->target ? stmt->target : names->class_name, stmt->method, names->class_name);
                                return 1;
                            
                            }
                            
                            if (call_idx == METHOD_NOT_FOUND) {
                            
                                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "unknown static method '%s.%s'", stmt->target ? stmt->target : names->class_name, stmt->method);
                                return 1;
                            
                            }
                            
                            dex_u2 (&buf, 0x0071);
                            dex_u2 (&buf, call_idx);
                            dex_u2 (&buf, 0x0000);
                            
                            dex_u2 (&buf, dex_move_result_opcode (method->return_type, 0));
                            dex_u2 (&buf, dex_return_opcode (method->return_type, 0));
                        
                        } else if (stmt->arg_kind == ARG_STRING) {
                        
                            unsigned long str_idx = dex_find_string (strings, nb_strings, stmt->arg_text);
                            
                            dex_u2 (&buf, 0x001A);
                            dex_u2 (&buf, str_idx);
                            
                            dex_u2 (&buf, dex_return_opcode (method->return_type, 0));
                        
                        } else if (stmt->arg_kind == ARG_INT) {
                        
                            dex_write_const_integer (&buf, method->return_type, 0, stmt->arg_text);
                            dex_u2 (&buf, dex_return_opcode (method->return_type, 0));
                        
                        } else if (stmt->arg_kind == ARG_ARITHMETIC) {
                        
                            const char *source_type;
                            
                            if (stmt->target[0] == '#') {
                            
                                unsigned long param_index = strtoul (stmt->target + 1, 0, 10);
                                unsigned long source_reg = method_param_register (method, registers_size, ins_size, param_index);
                                
                                source_type = method_param_type (method, param_index);
                                dex_u2 (&buf, 0x0001 | ((unsigned int) source_reg << 12));
                            
                            } else if (stmt->target[0] == '$') {
                            
                                unsigned long local_index = strtoul (stmt->target + 1, 0, 10);
                                unsigned long source_reg = method_local_register (method, local_index);
                                
                                source_type = method->locals[local_index].type;
                                dex_write_move_value (&buf, source_type[0], 0, source_reg);
                            
                            } else {
                            
                                struct field_info *source_field = find_field_info_by_name (names, stmt->target);
                                unsigned long source_idx = find_field_id_multi (all_names, field_map, nb_real_fields, field_id_idx, method_map[i].source_index, stmt->target);
                                
                                source_type = source_field->type;
                                
                                if (source_field->flags & METHOD_STATIC) {
                                    dex_u2 (&buf, dex_sget_opcode_for_type (source_type));
                                } else {
                                
                                    unsigned long this_reg = registers_size - ins_size;
                                    dex_u2 (&buf, dex_iget_opcode_for_type (source_type) | ((unsigned int) this_reg << 12));
                                
                                }
                                
                                dex_u2 (&buf, source_idx);
                            
                            }
                            
                            dex_write_mul_factor_list (&buf, method, source_type, method->return_type, 0, stmt->arg_text);
                            dex_u2 (&buf, dex_return_opcode (method->return_type, 0));
                        
                        } else {
                        
                            unsigned long return_reg = method_param_register (method, registers_size, ins_size, stmt->arg_param_index);
                            dex_u2 (&buf, dex_return_opcode (method->return_type, return_reg));
                        
                        }
                    
                    }
                
                }
                
                for (si = 0; si < method->nb_statements; si++) {
                
                    struct statement_info *stmt = &method->statements[si];
                    
                    if (stmt->kind == STMT_GOTO || stmt->kind == STMT_IF_FALSE || stmt->kind == STMT_IF_EQ_CONST) {
                    
                        long branch_offset;
                        unsigned long branch_pos;
                        
                        branch_offset = (long) method->statements[stmt->jump_target].code_offset - (long) stmt->branch_offset;
                        branch_pos = insns_start_pos + stmt->branch_offset * 2 + 2;
                        
                        dex_u2_at (&buf, branch_pos, (unsigned int) branch_offset & 0xffff);
                    
                    }
                
                }
                
                if (!has_return) {
                    dex_u2 (&buf, 0x000E);
                }
                
                insns_size = (dex_pos (&buf) - insns_start_pos) / 2;
                dex_u4_at (&buf, method_code_off[i] + 12, insns_size);
                
                if (method->nb_tries) {
                
                    unsigned long ti;
                    
                    if (insns_size & 1) {
                        dex_u2 (&buf, 0);
                    }
                    
                    for (ti = 0; ti < method->nb_tries; ti++) {
                    
                        struct try_info *tr = &method->tries[ti];
                        
                        dex_u4 (&buf, method->statements[tr->start_stmt].code_offset);
                        dex_u2 (&buf, method->statements[tr->end_stmt].code_offset - method->statements[tr->start_stmt].code_offset);
                        dex_u2 (&buf, ti == 0 ? 1 : 0);
                    
                    }
                    
                    dex_uleb128 (&buf, method->nb_tries);
                    
                    for (ti = 0; ti < method->nb_tries; ti++) {
                    
                        struct try_info *tr = &method->tries[ti];
                        
                        dex_uleb128 (&buf, 1);
                        dex_uleb128 (&buf, find_type_idx (types, nb_types, tr->type));
                        dex_uleb128 (&buf, method->statements[tr->handler_stmt].code_offset);
                    
                    }
                
                }
            
            }
        
        }
        
        (void) names;
    
    }
    
    for (i = 0; i < nb_sources; i++) {
    
        unsigned long *source_field_ids = xmalloc (all_names[i].nb_fields ? all_names[i].nb_fields * sizeof (*source_field_ids) : sizeof (*source_field_ids));
        
        for (j = 0; j < all_names[i].nb_fields; j++) {
        
            for (m = 0; m < nb_real_fields; m++) {
            
                if (field_map[m].source_index == i && field_map[m].field_index == j) {
                
                    source_field_ids[j] = field_id_idx[m];
                    break;
                
                }
            
            }
        
        }
        
        static_values_off[i] = dex_write_static_values (&buf, &all_names[i], source_field_ids, strings, nb_strings);
        
        if (static_values_off[i]) {
        
            if (!nb_static_value_arrays) {
                first_static_values_off = static_values_off[i];
            }
            
            nb_static_value_arrays++;
        
        }
        
        free (source_field_ids);
        
        dex_align4 (&buf);
        class_data_off[i] = dex_pos (&buf);
        
        {
        
            unsigned long nb_static_fields = 0;
            unsigned long nb_instance_fields = 0;
            unsigned long prev_field = 0;
            int have_prev = 0;
            
            for (j = 0; j < all_names[i].nb_fields; j++) {
            
                if (all_names[i].fields[j].flags & METHOD_STATIC) {
                    nb_static_fields++;
                } else {
                    nb_instance_fields++;
                }
            
            }
            
            dex_uleb128 (&buf, nb_static_fields);
            dex_uleb128 (&buf, nb_instance_fields);
            dex_uleb128 (&buf, all_names[i].nb_methods);
            dex_uleb128 (&buf, 0);
            
            for (j = 0; j < all_names[i].nb_fields; j++) {
            
                unsigned long global_field = 0;
                
                if (!(all_names[i].fields[j].flags & METHOD_STATIC)) {
                    continue;
                }
                
                for (m = 0; m < nb_real_fields; m++) {
                
                    if (field_map[m].source_index == i && field_map[m].field_index == j) {
                    
                        global_field = m;
                        break;
                    
                    }
                
                }
                
                dex_uleb128 (&buf, have_prev ? field_id_idx[global_field] - prev_field : field_id_idx[global_field]);
                dex_uleb128 (&buf, field_access_flags (&all_names[i].fields[j]));
                
                prev_field = field_id_idx[global_field];
                have_prev = 1;
            
            }
            
            prev_field = 0;
            have_prev = 0;
            
            for (j = 0; j < all_names[i].nb_fields; j++) {
            
                unsigned long global_field = 0;
                
                if (all_names[i].fields[j].flags & METHOD_STATIC) {
                    continue;
                }
                
                for (m = 0; m < nb_real_fields; m++) {
                
                    if (field_map[m].source_index == i && field_map[m].field_index == j) {
                    
                        global_field = m;
                        break;
                    
                    }
                
                }
                
                dex_uleb128 (&buf, have_prev ? field_id_idx[global_field] - prev_field : field_id_idx[global_field]);
                dex_uleb128 (&buf, field_access_flags (&all_names[i].fields[j]));
                
                prev_field = field_id_idx[global_field];
                have_prev = 1;
            
            }
        
        }
        
        for (j = 0; j < all_names[i].nb_methods; j++) {
        
            unsigned long global_method = 0;
            
            for (m = 0; m < nb_real_methods; m++) {
            
                if (method_map[m].source_index == i && method_map[m].method_index == j) {
                
                    global_method = m;
                    break;
                
                }
            
            }
            
            if (j == 0) {
                dex_uleb128 (&buf, method_id_idx[global_method]);
            } else {
            
                unsigned long prev_global = 0;
                
                for (m = 0; m < nb_real_methods; m++) {
                
                    if (method_map[m].source_index == i && method_map[m].method_index == j - 1) {
                    
                        prev_global = m;
                        break;
                    
                    }
                
                }
                
                dex_uleb128 (&buf, method_id_idx[global_method] - method_id_idx[prev_global]);
            
            }
            
            dex_uleb128 (&buf, all_names[i].methods[j].flags);
            dex_uleb128 (&buf, method_code_off[global_method]);
        
        }
    
    }
    
    dex_align4 (&buf);
    map_off = dex_pos (&buf);
    
    dex_u4 (&buf, 8 + (nb_fields ? 1 : 0) + (nb_type_lists ? 1 : 0));
    
    dex_u2 (&buf, 0x0000); dex_u2 (&buf, 0); dex_u4 (&buf, 1); dex_u4 (&buf, 0);
    dex_u2 (&buf, 0x0001); dex_u2 (&buf, 0); dex_u4 (&buf, nb_strings); dex_u4 (&buf, string_ids_off);
    dex_u2 (&buf, 0x0002); dex_u2 (&buf, 0); dex_u4 (&buf, nb_types); dex_u4 (&buf, type_ids_off);
    dex_u2 (&buf, 0x0003); dex_u2 (&buf, 0); dex_u4 (&buf, nb_protos); dex_u4 (&buf, proto_ids_off);
    
    if (nb_fields) {
        dex_u2 (&buf, 0x0004); dex_u2 (&buf, 0); dex_u4 (&buf, nb_fields); dex_u4 (&buf, field_ids_off);
    }
    
    dex_u2 (&buf, 0x0005); dex_u2 (&buf, 0); dex_u4 (&buf, nb_method_ids); dex_u4 (&buf, method_ids_off);
    dex_u2 (&buf, 0x0006); dex_u2 (&buf, 0); dex_u4 (&buf, nb_sources); dex_u4 (&buf, class_defs_off);
    
    if (nb_type_lists) {
        dex_u2 (&buf, 0x1001); dex_u2 (&buf, 0); dex_u4 (&buf, nb_type_lists); dex_u4 (&buf, type_lists_off);
    }
    
    dex_u2 (&buf, 0x2000); dex_u2 (&buf, 0); dex_u4 (&buf, nb_sources); dex_u4 (&buf, class_data_off[0]);
    dex_u2 (&buf, 0x2001); dex_u2 (&buf, 0); dex_u4 (&buf, nb_code_items); dex_u4 (&buf, code_items_off);
    
    if (nb_static_value_arrays) {
        dex_u2 (&buf, 0x2005); dex_u2 (&buf, 0); dex_u4 (&buf, nb_static_value_arrays); dex_u4 (&buf, first_static_values_off);
    }
    
    memcpy (buf.data, "dex\n035\0", 8);
    
    dex_u4_at (&buf, 32, buf.size);
    dex_u4_at (&buf, 36, header_size);
    dex_u4_at (&buf, 40, 0x12345678UL);
    dex_u4_at (&buf, 44, 0);
    dex_u4_at (&buf, 48, 0);
    dex_u4_at (&buf, 52, map_off);
    dex_u4_at (&buf, 56, nb_strings);
    dex_u4_at (&buf, 60, string_ids_off);
    dex_u4_at (&buf, 64, nb_types);
    dex_u4_at (&buf, 68, type_ids_off);
    dex_u4_at (&buf, 72, nb_protos);
    dex_u4_at (&buf, 76, proto_ids_off);
    dex_u4_at (&buf, 80, nb_fields);
    dex_u4_at (&buf, 84, nb_fields ? field_ids_off : 0);
    dex_u4_at (&buf, 88, nb_method_ids);
    dex_u4_at (&buf, 92, method_ids_off);
    dex_u4_at (&buf, 96, nb_sources);
    dex_u4_at (&buf, 100, class_defs_off);
    dex_u4_at (&buf, 104, buf.size - data_off);
    dex_u4_at (&buf, 108, data_off);
    
    for (i = 0; i < nb_strings; i++) {
        dex_u4_at (&buf, string_ids_off + i * 4, string_offsets[i]);
    }
    
    type_string_idx = xmalloc (nb_types * sizeof (*type_string_idx));
    
    for (i = 0; i < nb_types; i++) {
    
        type_string_idx[i] = dex_find_string (strings, nb_strings, types[i]);
        dex_u4_at (&buf, type_ids_off + i * 4, type_string_idx[i]);
    
    }
    
    for (i = 0; i < nb_real_methods; i++) {
    
        struct source_names *names = &all_names[method_map[i].source_index];
        struct method_info *method = &names->methods[method_map[i].method_index];
        
        unsigned long ret_type_idx = find_type_idx (types, nb_types, method->return_type);
        unsigned long shorty_idx = dex_find_string (strings, nb_strings, method->shorty);
        
        dex_u4_at (&buf, proto_ids_off + i * 12 + 0, shorty_idx);
        dex_u4_at (&buf, proto_ids_off + i * 12 + 4, ret_type_idx);
        dex_u4_at (&buf, proto_ids_off + i * 12 + 8, method_params_off[i]);
    
    }
    
    for (i = 0; i < nb_external_methods; i++) {
    
        unsigned long proto = external_methods[i].proto_idx;
        unsigned long ret_type_idx = find_type_idx (types, nb_types, external_methods[i].return_type);
        unsigned long shorty_idx = dex_find_string (strings, nb_strings, external_methods[i].shorty);
        
        dex_u4_at (&buf, proto_ids_off + proto * 12 + 0, shorty_idx);
        dex_u4_at (&buf, proto_ids_off + proto * 12 + 4, ret_type_idx);
        dex_u4_at (&buf, proto_ids_off + proto * 12 + 8, external_methods[i].params_off);
    
    }
    
    {
    
        unsigned long proto = nb_real_methods + nb_external_methods;
        unsigned long ret_type_idx = find_type_idx (types, nb_types, "V");
        unsigned long shorty_idx = dex_find_string (strings, nb_strings, "V");
        
        dex_u4_at (&buf, proto_ids_off + proto * 12 + 0, shorty_idx);
        dex_u4_at (&buf, proto_ids_off + proto * 12 + 4, ret_type_idx);
        dex_u4_at (&buf, proto_ids_off + proto * 12 + 8, 0);
    
    }
    
    for (i = 0; i < nb_real_fields; i++) {
    
        unsigned long off = field_ids_off + field_id_idx[i] * 8;
        
        dex_u2_at (&buf, off + 0, class_type_idx[field_map[i].source_index]);
        dex_u2_at (&buf, off + 2, field_type_idx[i]);
        dex_u4_at (&buf, off + 4, field_name_idx[i]);
    
    }
    
    for (i = 0; i < nb_real_methods; i++) {
    
        struct source_names *names = &all_names[method_map[i].source_index];
        struct method_info *method = &names->methods[method_map[i].method_index];
        unsigned long off = method_ids_off + i * 8;
        
        dex_u2_at (&buf, off + 0, class_type_idx[method_map[i].source_index]);
        dex_u2_at (&buf, off + 2, method_proto_idx[i]);
        dex_u4_at (&buf, off + 4, method_name_idx[i]);
        
        (void) names;
        (void) method;
    
    }
    
    for (i = 0; i < nb_external_methods; i++) {
    
        unsigned long off = method_ids_off + external_methods[i].method_id_idx * 8;
        
        dex_u2_at (&buf, off + 0, external_methods[i].class_type_idx);
        dex_u2_at (&buf, off + 2, external_methods[i].proto_idx);
        dex_u4_at (&buf, off + 4, external_methods[i].name_idx);
    
    }
    
    for (i = 0; i < nb_sources; i++) {
    
        unsigned long off = class_defs_off + i * 32;
        
        dex_u4_at (&buf, off + 0, class_type_idx[i]);
        dex_u4_at (&buf, off + 4, class_access_flags (&all_names[i]));
        dex_u4_at (&buf, off + 8, super_type_idx[i]);
        dex_u4_at (&buf, off + 12, interface_type_list_off[i]);
        dex_u4_at (&buf, off + 16, 0xFFFFFFFFUL);
        dex_u4_at (&buf, off + 20, 0);
        dex_u4_at (&buf, off + 24, class_data_off[i]);
        dex_u4_at (&buf, off + 28, static_values_off[i]);
    
    }
    
    dex_finish_hashes (&buf);
    
    if (fwrite (buf.data, 1, buf.size, fp) != buf.size) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok.start, tok.caret, "failed to write dex file");
        return 1;
    
    }
    
    for (i = 0; i < nb_sources; i++) {
        free (class_descs[i]);
    }
    
    free_external_methods (external_methods, nb_external_methods);
    
    free (interface_type_list_off);
    free (static_values_off);
    
    free (class_data_off);
    free (class_type_idx);
    free (class_descs);
    free (field_id_idx);
    free (field_type_idx);
    free (field_name_idx);
    free (field_map);
    free (method_code_off);
    free (type_string_idx);
    free (string_offsets);
    free (method_params_off);
    free (method_id_idx);
    free (method_name_idx);
    free (method_proto_idx);
    free (method_map);
    free (types);
    
    for (i = 0; i < nb_strings; i++) {
        free ((char *) strings[i].text);
    }
    
    free (strings);
    free (buf.data);

    return 0;

}

static int compile_single_out_stage (int src_count, char **src_path);

static int compile_one_source (const char *src_path) {

    char *paths[1];
    
    paths[0] = (char *) src_path;
    return compile_single_out_stage (1, paths);

}

static unsigned long count_source_tree (struct source_names *names) {

    unsigned long count = 1;
    unsigned long i;
    
    for (i = 0; i < names->nb_nested_classes; i++) {
        count += count_source_tree (&names->nested_classes[i]);
    }
    
    return count;

}

static void flatten_source_tree (struct source_names *names, struct source_names *flat, unsigned long *index) {

    unsigned long i;
    flat[(*index)++] = *names;
    
    for (i = 0; i < names->nb_nested_classes; i++) {
        flatten_source_tree (&names->nested_classes[i], flat, index);
    }

}

static int compile_single_out_stage (int src_count, char **src_path) {

    struct source_names *all_names;
    struct source_names *pre_names;
    
    char **declaration_super_classes = 0;
    int ret = 0, i, j;
    
    unsigned char *valid_declarations = 0;
    
    if (src_count <= 0) {
        return 1;
    }
    
    pre_names = xmalloc (src_count * sizeof (*pre_names));
    memset (pre_names, 0, src_count * sizeof (*pre_names));
    
    collect_declarations_only = 1;
    
    for (i = 0; i < src_count; i++) {
    
        if (collect_source_names (src_path[i], &pre_names[i])) {
            ret = 1;
        }
    
    }
    
    collect_declarations_only = 0;
    
    if (!ret) {
    
        unsigned long index = 0;
        
        for (i = 0; i < src_count; i++) {
            nb_declaration_names += count_source_tree (&pre_names[i]);
        }
        
        declaration_names = xmalloc (nb_declaration_names * sizeof (*declaration_names));
        declaration_super_classes = xmalloc (nb_declaration_names * sizeof (*declaration_super_classes));
        
        for (i = 0; i < src_count; i++) {
            flatten_source_tree (&pre_names[i], declaration_names, &index);
        }
        
        for (i = 0; i < (int) nb_declaration_names; i++) {
            declaration_super_classes[i] = declaration_names[i].super_class;
        }
    
    }
    
    if (!ret) {
    
        valid_declarations = xmalloc (src_count * sizeof (*valid_declarations));
        memset (valid_declarations, 0, src_count * sizeof (*valid_declarations));
        
        validate_declarations_only = 1;
        
        for (i = 0; i < src_count; i++) {
        
            struct source_names checked_names;
            memset (&checked_names, 0, sizeof (checked_names));
            
            if (collect_source_names (src_path[i], &checked_names)) {
                ret = 1;
            } else {
                valid_declarations[i] = 1;
            }
            
            free_source_names (&checked_names);
        
        }
        
        validate_declarations_only = 0;
    
    }
    
    if (ret && valid_declarations) {
    
        for (i = 0; i < src_count; i++) {
        
            struct source_names checked_names;
            
            if (!valid_declarations[i]) {
                continue;
            }
            
            memset (&checked_names, 0, sizeof (checked_names));
            
            collect_source_names (src_path[i], &checked_names);
            free_source_names (&checked_names);
        
        }
    
    }
    
    free (valid_declarations);
    
    if (ret) {
    
        for (i = 0; i < src_count; i++) {
            free_source_names (&pre_names[i]);
        }
        
        if (declaration_names) {
        
            for (i = 0; i < (int) nb_declaration_names; i++) {
            
                if (declaration_names[i].super_class != declaration_super_classes[i]) {
                    free (declaration_names[i].super_class);
                }
            
            }
            
            free (declaration_super_classes);
            free (declaration_names);
            
            declaration_names = 0;
            nb_declaration_names = 0;
        
        }
        
        free (pre_names);
        return ret || get_error_count () != 0;
    
    }
    
    all_names = xmalloc (src_count * sizeof (*all_names));
    memset (all_names, 0, src_count * sizeof (*all_names));
    
    for (i = 0; i < src_count; i++) {
    
        if (collect_source_names (src_path[i], &all_names[i])) {
        
            ret = 1;
            break;
        
        }
    
    }
    
    if (!ret) {
    
        struct source_names *flat_names;
        char **original_super_classes;
        
        unsigned long flat_count = 0;
        unsigned long flat_index = 0;
        
        for (i = 0; i < src_count; i++) {
            flat_count += count_source_tree (&all_names[i]);
        }
        
        flat_names = xmalloc (flat_count * sizeof (*flat_names));
        original_super_classes = xmalloc (flat_count * sizeof (*original_super_classes));
        
        for (i = 0; i < src_count; i++) {
            flatten_source_tree (&all_names[i], flat_names, &flat_index);
        }
        
        for (i = 0; i < (int) flat_count; i++) {
            original_super_classes[i] = flat_names[i].super_class;
        }
        
        ret = semantic_check_sources (flat_names, flat_count);
        
        if (!ret) {
            ret = write_dex_files (state->ofp, flat_names, flat_count);
        }
        
        for (i = 0; i < (int) flat_count; i++) {
        
            if (flat_names[i].super_class != original_super_classes[i]) {
                free (flat_names[i].super_class);
            }
        
        }
        
        free (original_super_classes);
        free (flat_names);
    
    }
    
    for (j = 0; j < src_count; j++) {
        free_source_names (&all_names[j]);
    }
    
    free (all_names);
    
    for (i = 0; i < (int) nb_declaration_names; i++) {
    
        if (declaration_names[i].super_class != declaration_super_classes[i]) {
            free (declaration_names[i].super_class);
        }
    
    }
    
    free (declaration_super_classes);
    free (declaration_names);
    
    declaration_names = 0;
    nb_declaration_names = 0;
    
    for (i = 0; i < src_count; i++) {
        free_source_names (&pre_names[i]);
    }
    
    free (pre_names);
    return ret;

}

int compile_main (int argc, char **argv) {

    if (argc == 3) {
        return compile_one_source (argv[1]);
    }
    
    if (argc >= 4 && strcmp (argv[1], "--single-out") == 0) {
        return compile_single_out_stage (argc - 3, &argv[3]);
    }
    
    return 1;

}
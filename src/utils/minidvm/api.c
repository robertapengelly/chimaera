/******************************************************************************
 * @file            api.c
 *****************************************************************************/
#include "mini_dalvik_vm.h"

ApiDB g_api;

void api_init (ApiDB *db) {

    if (db != NULL) {
    
        db->methods = NULL;
        db->fields = NULL;
        db->loaded = 0;
    
    }

}

void api_free (ApiDB *db) {

    ApiMethod *m;
    ApiField *f;
    ApiMethod *mn;
    ApiField *fn;
    
    if (db == NULL) {
        return;
    }
    
    m = db->methods;
    
    while (m != NULL) {
    
        mn = m->next;
        
        free (m);
        m = mn;
    
    }
    
    f = db->fields;
    
    while (f != NULL) {
    
        fn = f->next;
        
        free (f);
        f = fn;
    
    }
    
    api_init (db);

}

static char *trim_ws (char *s) {

    char *e;
    
    if (s == NULL) {
        return NULL;
    }
    
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    
    e = s + strlen (s);
    
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        e--;
    }
    
    *e = '\0';
    return s;

}

static void safe_copy (char *dst, size_t dstsz, const char *src) {

    size_t n;
    
    if (dst == NULL || dstsz == 0) {
        return;
    }
    
    if (src == NULL) {
    
        dst[0] = '\0';
        return;
    
    }
    
    n = strlen (src);
    
    if (n >= dstsz) {
        n = dstsz - 1;
    }
    
    memcpy (dst, src, n);
    dst[n] = '\0';

}

int api_builtin_method_id (const char *cls, const char *name, const char *params, const char *ret) {

    if (cls == NULL || name == NULL || params == NULL || ret == NULL) {
        return BUILTIN_NONE;
    }
    
    if (strcmp (cls, "java.lang.Math") == 0 && (strcmp (name, "random") == 0 || strcmp (name, "rand") == 0) && strcmp (params, "()") == 0 && (strcmp (ret, "double") == 0)) {
        return BUILTIN_MATH_RANDOM;
    }
    
    if (strcmp (cls, "java.lang.StringBuilder") == 0 && strcmp (name, "<init>") == 0 && strcmp (params, "()") == 0 && strcmp (ret, "void") == 0) {
        return BUILTIN_SB_INIT;
    }
    
    if (strcmp (cls, "java.lang.StringBuilder") == 0 && strcmp (name, "append") == 0 && strcmp (params, "(java.lang.String)") == 0 && strcmp (ret, "java.lang.StringBuilder") == 0) {
        return BUILTIN_SB_APPEND_STRING;
    }
    
    if (strcmp (cls, "java.lang.StringBuilder") == 0 && strcmp (name, "append") == 0 && strcmp (params, "(int)") == 0 && strcmp (ret, "java.lang.StringBuilder") == 0) {
        return BUILTIN_SB_APPEND_INT;
    }
    
    if (strcmp (cls, "java.lang.StringBuilder") == 0 && strcmp (name, "append") == 0 && strcmp (params, "(long)") == 0 && strcmp (ret, "java.lang.StringBuilder") == 0) {
        return BUILTIN_SB_APPEND_LONG;
    }
    
    if (strcmp (cls, "java.lang.StringBuilder") == 0 && strcmp (name, "append") == 0 && strcmp (params, "(char)") == 0 && strcmp (ret, "java.lang.StringBuilder") == 0) {
        return BUILTIN_SB_APPEND_CHAR;
    }
    
    if (strcmp (cls, "java.lang.StringBuilder") == 0 && strcmp (name, "append") == 0 && strcmp (params, "(float)") == 0 && strcmp (ret, "java.lang.StringBuilder") == 0) {
        return BUILTIN_SB_APPEND_FLOAT;
    }
    
    if (strcmp (cls, "java.lang.StringBuilder") == 0 && strcmp (name, "append") == 0 && strcmp (params, "(double)") == 0 && strcmp (ret, "java.lang.StringBuilder") == 0) {
        return BUILTIN_SB_APPEND_DOUBLE;
    }
    
    if (strcmp (cls, "java.lang.StringBuilder") == 0 && strcmp (name, "toString") == 0 && strcmp (params, "()") == 0 && strcmp (ret, "java.lang.String") == 0) {
        return BUILTIN_SB_TO_STRING;
    }
    
    if (strcmp (cls, "java.io.PrintStream") == 0 && strcmp (name, "Java_Core_println") == 0 && strcmp (params, "(java.lang.String)") == 0 && strcmp (ret, "void") == 0) {
        return BUILTIN_PRINTLN_STRING;
    }
    
    if (strcmp (cls, "java.io.PrintStream") == 0 && strcmp (name, "Java_Core_println") == 0 && strcmp (params, "(int)") == 0 && strcmp (ret, "void") == 0) {
        return BUILTIN_PRINTLN_INT;
    }
    
    if (strcmp (cls, "java.io.PrintStream") == 0 && strcmp (name, "Java_Core_println") == 0 && strcmp (params, "(double)") == 0 && strcmp (ret, "void") == 0) {
        return BUILTIN_PRINTLN_DOUBLE;
    }
    
    if (strcmp (cls, "android.util.Log") == 0 && strcmp (name, "i") == 0 && strcmp (params, "(java.lang.String,java.lang.String)") == 0 && strcmp (ret, "int") == 0) {
        return BUILTIN_LOG_I;
    }
    
    if (strcmp (cls, "android.util.Log") == 0 && strcmp (name, "d") == 0 && strcmp (params, "(java.lang.String,java.lang.String)") == 0 && strcmp (ret, "int") == 0) {
        return BUILTIN_LOG_D;
    }
    
    return BUILTIN_NONE;

}

static int api_builtin_field_id (const char *cls, const char *name, const char *type) {

    if (cls == NULL || name == NULL || type == NULL) {
        return BUILTIN_NONE;
    }
    
    if (strcmp (cls, "java.lang.System") == 0 && strcmp (name, "out") == 0 && strcmp (type, "java.io.PrintStream") == 0) {
        return BUILTIN_FIELD_SYSTEM_OUT;
    }
    
    return BUILTIN_NONE;

}

static int api_add_method (ApiDB *db, const char *cls, const char *name, const char *params, const char *ret) {

    ApiMethod *m;
    
    if (db == NULL || cls == NULL || name == NULL || params == NULL || ret == NULL) {
        return 0;
    }
    
    m = (ApiMethod *) calloc (1, sizeof (*m));
    
    if (m == NULL) {
        return 0;
    }
    
    safe_copy (m->class_name, sizeof (m->class_name), cls);
    safe_copy (m->method_name, sizeof (m->method_name), name);
    safe_copy (m->params, sizeof (m->params), params);
    safe_copy (m->ret_type, sizeof (m->ret_type), ret);
    
    m->builtin_id = api_builtin_method_id (cls, name, params, ret);
    m->next = db->methods;
    
    db->methods = m;
    return 1;

}

static int api_add_field (ApiDB *db, const char *cls, const char *name, const char *type) {

    ApiField *f;
    
    if (db == NULL || cls == NULL || name == NULL || type == NULL) {
        return 0;
    }
    
    f = (ApiField *) calloc (1, sizeof (*f));
    
    if (f == NULL) {
        return 0;
    }
    
    safe_copy (f->class_name, sizeof (f->class_name), cls);
    safe_copy (f->field_name, sizeof (f->field_name), name);
    safe_copy (f->field_type, sizeof (f->field_type), type);
    
    f->builtin_id = api_builtin_field_id (cls, name, type);
    f->next = db->fields;
    
    db->fields = f;
    return 1;

}

void api_add_core_builtins (ApiDB *db) {

    if (db == NULL) {
        return;
    }
    
    api_add_field (db, "java.lang.System", "out", "java.io.PrintStream");
    
    api_add_method (db, "java.io.PrintStream", "println", "(java.lang.String)", "void");
    api_add_method (db, "java.io.PrintStream", "println", "(int)", "void");
    api_add_method (db, "java.io.PrintStream", "println", "(double)", "void");
    api_add_method (db, "java.lang.StringBuilder", "<init>", "()", "void");
    api_add_method (db, "java.lang.StringBuilder", "append", "(java.lang.String)", "java.lang.StringBuilder");
    api_add_method (db, "java.lang.StringBuilder", "append", "(int)", "java.lang.StringBuilder");
    api_add_method (db, "java.lang.StringBuilder", "append", "(long)", "java.lang.StringBuilder");
    api_add_method (db, "java.lang.StringBuilder", "append", "(char)", "java.lang.StringBuilder");
    api_add_method (db, "java.lang.StringBuilder", "append", "(float)", "java.lang.StringBuilder");
    api_add_method (db, "java.lang.StringBuilder", "append", "(double)", "java.lang.StringBuilder");
    api_add_method (db, "java.lang.StringBuilder", "toString", "()", "java.lang.String");
    api_add_method (db, "java.lang.Math", "random", "()", "double");
    api_add_method (db, "java.lang.Object", "<init>", "()", "void");
    
    db->loaded = 1;

}

int api_load_file (ApiDB *db, const char *path) {

    char line[1024];
    FILE *fp;
    
    if (db == NULL || path == NULL) {
        return 0;
    }
    
    fp = fopen (path, "r");
    
    if (fp == NULL) {
        return 0;
    }
    
    while (fgets (line, sizeof (line), fp) != NULL) {
    
        char *s = trim_ws (line);
        
        if (*s == '\0' || *s == '#') {
            continue;
        }
        
        if (strncmp (s, "class ", 6) == 0) {
        
            db->loaded = 1;
            continue;
        
        }
        
        if (strncmp (s, "field ", 6) == 0) {
        
            char cls[128], name[64], type[128];
            
            if (sscanf (s + 6, "%127s %63s %127s", cls, name, type) == 3) {
            
                api_add_field (db, cls, name, type);
                db->loaded = 1;
            
            }
            
            continue;
        
        }
        
        if (strncmp (s, "method ", 7) == 0) {
        
            char cls[128], name[64], ret[128];
            char *p = s + 7;
            
            char *lpar;
            char *rpar;
            
            char params[256];
            
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            
            lpar = strchr (p, '(');
            rpar = strchr (p, ')');
            
            if (lpar == NULL || rpar == NULL || rpar < lpar) {
                continue;
            }
            
            {
                char head[256];
                size_t hn = (size_t)(lpar - p);
                
                if (hn >= sizeof (head)) {
                    hn = sizeof (head) - 1;
                }
                
                memcpy (head, p, hn);
                head[hn] = '\0';
                
                if (sscanf (head, "%127s %63s", cls, name) != 2) {
                    continue;
                }
            
            }
            
            {
            
                size_t pn = (size_t)(rpar - lpar + 1);
                
                if (pn >= sizeof (params)) {
                    pn = sizeof (params) - 1;
                }
                
                memcpy (params, lpar, pn);
                params[pn] = '\0';
            
            }
            
            if (sscanf (rpar + 1, "%127s", ret) != 1) {
                continue;
            }
            
            api_add_method (db, cls, name, params, ret);
            
            db->loaded = 1;
            continue;
        
        }
    
    }
    
    fclose (fp);
    return db->loaded;

}

const ApiMethod *api_find_method (const ApiDB *db, const char *cls, const char *name, const char *params) {

    const ApiMethod *m;
    
    if (db == NULL || cls == NULL || name == NULL || params == NULL) {
        return NULL;
    }
    
    for (m = db->methods; m != NULL; m = m->next) {
    
        if (strcmp (m->class_name, cls) == 0 && strcmp (m->method_name, name) == 0 && strcmp (m->params, params) == 0) {
            return m;
        }
    
    }
    
    return NULL;

}

const ApiField *api_find_field (const ApiDB *db, const char *cls, const char *name) {

    const ApiField *f;
    
    if (db == NULL || cls == NULL || name == NULL) {
        return NULL;
    }
    
    for (f = db->fields; f != NULL; f = f->next) {
    
        if (strcmp (f->class_name, cls) == 0 && strcmp (f->field_name, name) == 0) {
            return f;
        }
    
    }
    
    return NULL;

}

void desc_to_java_name (const char *desc, char *out, size_t out_sz) {

    size_t j;
    
    if (out == NULL || out_sz == 0) {
        return;
    }
    
    out[0] = '\0';
    
    if (desc == NULL || *desc == '\0') {
        return;
    }
    
    if (strcmp (desc, "V") == 0) {
    
        safe_copy (out, out_sz, "void");
        return;
    
    }
    
    if (strcmp (desc, "Z") == 0) {
    
        safe_copy (out, out_sz, "boolean");
        return;
    
    }
    
    if (strcmp (desc, "B") == 0) {
    
        safe_copy (out, out_sz, "byte");
        return;
    
    }
    
    if (strcmp (desc, "C") == 0) {
    
        safe_copy (out, out_sz, "char");
        return;
    
    }
    
    if (strcmp (desc, "S") == 0) {
    
        safe_copy (out, out_sz, "short");
        return;
    
    }
    
    if (strcmp (desc, "I") == 0) {
    
        safe_copy (out, out_sz, "int");
        return;
    
    }
    
    if (strcmp (desc, "J") == 0) {
    
        safe_copy (out, out_sz, "long");
        return;
    
    }
    
    if (strcmp (desc, "F") == 0) {
    
        safe_copy (out, out_sz, "float");
        return;
    
    }
    
    if (strcmp (desc, "D") == 0) {
    
        safe_copy (out, out_sz, "double");
        return;
    
    }
    
    if (desc[0] == 'L') {
    
        j = 0;
        desc++;
        
        while (*desc != '\0' && *desc != ';' && j + 1 < out_sz) {
        
            out[j++] = (*desc == '/') ? '.' : *desc;
            desc++;
        
        }
        
        out[j] = '\0';
        return;
    
    }
    
    if (desc[0] == '[') {
    
        char base[128];
        
        desc_to_java_name (desc + 1, base, sizeof (base));
        snprintf (out, out_sz, "%s[]", base);
        
        return;
    
    }
    
    safe_copy (out, out_sz, desc);

}

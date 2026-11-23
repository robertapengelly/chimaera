/******************************************************************************
 * @file            builtin.c
 *****************************************************************************/
#include "mini_dalvik_vm.h"

int call_builtin (const DexFile *dex, u4 method_idx, Value *args, int argc, Value *out) {

    const char *cls;
    const char *name;
    const char *ret_desc;
    
    u2 proto_idx;
    
    char cls_java[128];
    char params_sig[256];
    char ret_java[128];
    
    const ApiMethod *api_m;
    int builtin_id;
    
    if (out == NULL) {
        return 0;
    }
    
    *out = value_int (0);
    
    if (!dex_method_name_parts (dex, method_idx, &cls, &name, &proto_idx)) {
        return 0;
    }
    
    if (!build_param_sig_text (dex, proto_idx, params_sig, sizeof (params_sig))) {
        return 0;
    }
    
    if (!dex_proto_return_desc (dex, proto_idx, &ret_desc)) {
        return 0;
    }
    
    desc_to_java_name (cls, cls_java, sizeof (cls_java));
    desc_to_java_name (ret_desc, ret_java, sizeof (ret_java));
    
    if (strcmp (cls_java, "Core") == 0 && strcmp (name, "Java_Core_random") == 0 && strcmp (params_sig, "()") == 0 && strcmp (ret_java, "double") == 0) {
    
        double r = (double) rand () / ((double) RAND_MAX + 1.0);
        
        *out = value_double (r);
        return 1;
    
    }
    
    if (strcmp (cls_java, "Core") == 0 && strcmp (name, "Java_Core_println") == 0 && strcmp (params_sig, "(java.lang.String)") == 0) {
    
        int i;
        
        for (i = 0; i < argc; i++) {
        
            if (args[i].kind == VAL_OBJ && args[i].obj != NULL && args[i].obj->type == OBJ_STRING) {
            
                printf ("%s\n", args[i].obj->u.string.text);
                
                *out = value_int (0);
                return 1;
            
            }
            
            if (args[i].kind == VAL_OBJ && args[i].obj != NULL && args[i].obj->type == OBJ_STRING_BUILDER) {

                printf ("%s\n", args[i].obj->u.sb.text);
                
                *out = value_int (0);
                return 1;
            
            }
        
        }
        
        printf ("null\n");
        
        *out = value_int (0);
        return 1;
    
    }
    
    builtin_id = api_builtin_method_id (cls_java, name, params_sig, ret_java);
    
    if (builtin_id == BUILTIN_NONE) {
    
        if (!g_api.loaded) {
            return 0;
        }
        
        api_m = api_find_method (&g_api, cls_java, name, params_sig);
        
        if (api_m == NULL) {
            return 0;
        }
        
        builtin_id = api_m->builtin_id;
    
    }
    
    switch (builtin_id) {
    
        case BUILTIN_MATH_RANDOM:
        
            double r = (double) rand () / ((double) RAND_MAX + 1.0);
            
            *out = value_double (r);
            return 1;
        
        case BUILTIN_SB_INIT:
        
            if (argc >= 1 && args[0].kind == VAL_OBJ && args[0].obj != NULL) {
            
                args[0].obj->type = OBJ_STRING_BUILDER;
                args[0].obj->u.sb.text[0] = '\0';
                
                *out = args[0];
                return 1;
            
            }
            
            return 0;
        
        case BUILTIN_SB_APPEND_STRING:
        
            if (argc >= 2 && args[0].kind == VAL_OBJ && args[0].obj != NULL && args[0].obj->type == OBJ_STRING_BUILDER && args[1].kind == VAL_OBJ && args[1].obj != NULL && args[1].obj->type == OBJ_STRING) {
            
                sb_append_text (args[0].obj, args[1].obj->u.string.text);
                
                *out = args[0];
                return 1;
            
            }
            
            return 0;
        
        case BUILTIN_SB_APPEND_INT:
        
            if (argc >= 2 && args[0].kind == VAL_OBJ && args[0].obj != NULL && args[0].obj->type == OBJ_STRING_BUILDER && (args[1].kind == VAL_INT || args[1].kind == VAL_WIDE)) {
            
                sb_append_long (args[0].obj, args[1].kind == VAL_WIDE ? (long)(long long) args[1].wide : args[1].i);
                
                *out = args[0];
                return 1;
            
            }
            
            return 0;
        
        case BUILTIN_SB_APPEND_LONG:
        
            if (argc >= 2 && args[0].kind == VAL_OBJ && args[0].obj != NULL && args[0].obj->type == OBJ_STRING_BUILDER) {
            
                if (args[1].kind == VAL_INT || args[1].kind == VAL_WIDE) {
                    sb_append_long (args[0].obj, args[1].kind == VAL_WIDE ? (long)(long long) args[1].wide : args[1].i);
                } else if (args[1].kind == VAL_OBJ || args[1].kind == VAL_UNINIT) {
                    sb_append_text (args[0].obj, "null");
                } else {
                    return 0;
                }
                
                *out = args[0];
                return 1;
            
            }
            
            return 0;
        
        case BUILTIN_SB_APPEND_CHAR:
        
            if (argc >= 2 && args[0].kind == VAL_OBJ && args[0].obj != NULL && args[0].obj->type == OBJ_STRING_BUILDER && args[1].kind == VAL_INT) {
            
                char text[2];
                
                text[0] = (char) (args[1].i & 0xff);
                text[1] = '\0';
                
                sb_append_text (args[0].obj, text);
                
                *out = args[0];
                return 1;
            
            }
            
            return 0;
        
        case BUILTIN_SB_APPEND_FLOAT:
        
            if (argc >= 2 && args[0].kind == VAL_OBJ && args[0].obj != NULL && args[0].obj->type == OBJ_STRING_BUILDER) {
            
                float f;
                
                if (args[1].kind == VAL_DOUBLE) {
                    f = (float) args[1].d;
                } else if (args[1].kind == VAL_INT) {
                
                    u4 bits = (u4) args[1].i;
                    memcpy (&f, &bits, sizeof (f));
                
                } else {
                    return 0;
                }
                
                sb_append_double (args[0].obj, (double)f);
                
                *out = args[0];
                return 1;
            
            }
            
            return 0;
        
        case BUILTIN_SB_APPEND_DOUBLE:
        
            if (argc >= 2 && args[0].kind == VAL_OBJ && args[0].obj != NULL && args[0].obj->type == OBJ_STRING_BUILDER && (args[1].kind == VAL_DOUBLE || args[1].kind == VAL_WIDE)) {
            
                double d;
                
                if (args[1].kind == VAL_WIDE) {
                
                    unsigned long long bits = args[1].wide;
                    memcpy (&d, &bits, sizeof (d));
                
                } else {
                    d = args[1].d;
                }
                
                sb_append_double (args[0].obj, d);
                
                *out = args[0];
                return 1;
            
            }
            
            return 0;
        
        case BUILTIN_SB_TO_STRING:
        
            if (argc >= 1 && args[0].kind == VAL_OBJ && args[0].obj != NULL && args[0].obj->type == OBJ_STRING_BUILDER) {
            
                Obj *s = obj_new_string (args[0].obj->u.sb.text);
                
                if (s == NULL) {
                    return 0;
                }
                
                *out = value_obj (s);
                return 1;
            
            }
            
            return 0;
        
        case BUILTIN_PRINTLN_STRING:
        
            if (argc >= 2 && args[0].kind == VAL_OBJ && args[0].obj != NULL && args[0].obj->type == OBJ_PRINT_STREAM && args[1].kind == VAL_OBJ && args[1].obj != NULL && args[1].obj->type == OBJ_STRING) {
            
                printf ("%s\n", args[1].obj->u.string.text);
                return 1;
            
            }
            
            if (argc >= 1) {
            
                printf ("null\n");
                return 1;
            
            }
            
            return 0;
        
        case BUILTIN_PRINTLN_INT:
        
            if (argc >= 2 && args[0].kind == VAL_OBJ && args[0].obj != NULL && args[0].obj->type == OBJ_PRINT_STREAM && args[1].kind == VAL_INT) {
            
                printf ("%ld\n", args[1].i);
                return 1;
            
            }
            
            return 0;
        
        case BUILTIN_PRINTLN_DOUBLE:
        
            if (argc >= 2 && args[0].kind == VAL_OBJ && args[0].obj != NULL && args[0].obj->type == OBJ_PRINT_STREAM && args[1].kind == VAL_DOUBLE) {
            
                printf ("%g\n", args[1].d);
                return 1;
            
            }
            
            return 0;
        
        case BUILTIN_LOG_I:     case BUILTIN_LOG_D:
        
            if (argc >= 1) {
            
                Value *msg = &args[argc - 1];
                
                if (msg->kind == VAL_OBJ && msg->obj != NULL && msg->obj->type == OBJ_STRING) {
                    printf ("%s\n", msg->obj->u.string.text);
                } else if (msg->kind == VAL_INT) {
                    printf ("%ld\n", msg->i);
                } else if (msg->kind == VAL_DOUBLE) {
                    printf ("%g\n", msg->d);
                }
            
            }
            
            *out = value_int (0);
            return 1;
    
    }

    return 0;

}

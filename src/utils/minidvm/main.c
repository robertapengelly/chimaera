/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include "mini_dalvik_vm.h"

static void print_ret (const Value *v) {

    if (v == NULL) {
        return;
    }
    
    if (v->kind == VAL_INT) {
        printf ("Returned: %ld\n", v->i);
    } else if (v->kind == VAL_DOUBLE) {
        printf ("Returned: %g\n", v->d);
    } else if (v->kind == VAL_OBJ && v->obj != NULL && v->obj->type == OBJ_STRING) {
        printf ("Returned: %s\n", v->obj->u.string.text);
    } else {
        printf ("Returned: <object>\n");
    }

}

static void print_usage (const char *argv0) {

    fprintf (stderr, "Usage:\n");
    fprintf (stderr, "  %s --list Foo1.dex\n", argv0);
    fprintf (stderr, "  %s Foo1.dex 'LFoo1;' main\n", argv0);
    fprintf (stderr, "  %s -t Foo1.dex 'LFoo1;' main\n", argv0);

}

int main (int argc, char **argv) {

    DexFile dex;
    Value ret;
    
    int trace;
    
    const char *path;
    const char *class_desc;
    const char *method_name;
    const char *api_path;
    
    trace = 0;
    srand (time (0));
    
    api_init (&g_api);
    api_add_core_builtins (&g_api);
    
    api_path = getenv ("DVM_API_FILE");
    
    if (api_path == NULL || *api_path == '\0') {
        api_path = "api.txt";
    }
    
    api_load_file (&g_api, api_path);
    
    if (argc == 3 && strcmp (argv[1], "--list") == 0) {
    
        if (!dex_load_file (argv[2], &dex)) {
        
            api_free (&g_api);
            return 1;
        }
        
        dex_list_methods (&dex);
        
        dex_free (&dex);
        api_free (&g_api);
        
        return 0;
    
    }
    
    if (argc == 4) {
    
        path = argv[1];
        class_desc = argv[2];
        method_name = argv[3];
    
    } else if (argc == 5 && strcmp(argv[1], "-t") == 0) {
    
        trace = 1;
        
        path = argv[2];
        class_desc = argv[3];
        method_name = argv[4];
    
    } else {
    
        print_usage (argv[0]);
        
        api_free (&g_api);
        return 1;
    
    }
    
    if (!dex_load_file(path, &dex)) {
    
        api_free (&g_api);
        return 1;
    
    }
    
    /* Set the base path based on the primary dex location */
    set_base_path (path);
    
    /* Add to registry so find_method_in_any_dex can see it */
    g_loaded_dexes[g_dex_count++] = dex;
    
    if (!initialise_static_fields(&dex)) {
    
        fprintf (stderr, "could not initialise static fields\n");
        
        api_free (&g_api);
        dex_free (&dex);
        
        return 1;
    
    }
    
    if (strcmp (method_name, "<clinit>") != 0) {
    
        u4 clinit_method_idx;
        u4 clinit_code_off;
        Value clinit_ret;
        
        if (dex_find_method_code (&dex, class_desc, "<clinit>", &clinit_method_idx, &clinit_code_off) && clinit_code_off != 0) {
        
            if (!exec_method_by_code( &dex, class_desc, "<clinit>", clinit_method_idx, clinit_code_off, trace, &clinit_ret)) {
            
                api_free (&g_api);
                dex_free (&dex);
                
                return 1;
            
            }
        
        }
    
    }
    
    if (!exec_method (&dex, class_desc, method_name, trace, &ret)) {
    
        if (g_pending_exception != NULL) {
        
            if (g_pending_exception->class_desc != NULL) {
                fprintf (stderr, "Unhandled exception: %s\n", g_pending_exception->class_desc);
            } else {
                fprintf (stderr, "Unhandled exception: <object>\n");
            }
            
            g_pending_exception = NULL;
        
        }
        
        api_free (&g_api);
        dex_free (&dex);
        
        return 1;
    
    }
    
    print_ret (&ret);
    dex_free (&dex);
    
    return 0;

}

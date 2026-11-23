/******************************************************************************
 * @file            loader.c
 *****************************************************************************/
#include "mini_dalvik_vm.h"

char g_base_path[256] = "";
int g_dex_count = 0;

DexFile g_loaded_dexes[MAX_LOADED_DEX];

void set_base_path (const char *path) {

    const char *last_slash = strrchr (path, '/');
    
    if (last_slash != NULL) {
    
        size_t len = (size_t) (last_slash - path + 1);
        
        if (len < sizeof (g_base_path)) {
        
            memcpy (g_base_path, path, len);
            g_base_path[len] = '\0';
        
        }
    
    } else {
        g_base_path[0] = '\0';          /* No directory component */
    }

}

const DexFile* find_dex_for_class (const char* class_desc) {

    for (int i = 0; i < g_dex_count; i++) {
    
        for (u4 class_i = 0; class_i < g_loaded_dexes[i].class_defs_size; class_i++) {
        
            DexClassDef cls;
            const char *desc;
            
            if (dex_get_class_def (&g_loaded_dexes[i], class_i, &cls)) {
            
                if (dex_get_type_descriptor (&g_loaded_dexes[i], cls.class_idx, &desc)) {
                
                    if (strcmp (desc, class_desc) == 0) {
                        return &g_loaded_dexes[i];
                    }
                
                }
            
            }
        
        }
    
    }
    
    return NULL;

}

int try_dynamic_load (const char *class_desc) {

    char full_path[512];
    char fallback_name[256];
    
    const char *last_slash, *start_ptr;
    size_t name_len;
    
    if (strncmp (class_desc, "Ljava/", 6) == 0 || strncmp (class_desc, "Landroid/", 9) == 0) {
        return 0; 
    }
    
    if (class_desc[0] != 'L') {
        return 0;
    }

    last_slash = strrchr (class_desc, '/');
    start_ptr = last_slash ? last_slash + 1 : class_desc + 1;

    name_len = strlen (start_ptr);
    
    if (name_len < 2) {
        return 0;
    }
    
    /* Create the basic filename (e.g., "Hello.dex") */
    memcpy (fallback_name, start_ptr, name_len - 1);
    fallback_name[name_len - 1] = '\0';
    
    strcat (fallback_name, ".dex");
    
    /* Create the full path relative to the primary DEX */
    snprintf (full_path, sizeof (full_path), "%s%s", g_base_path, fallback_name);
    
    if (g_dex_count >= MAX_LOADED_DEX) {
        return 0;
    }
    
    /* 1. Try loading from the primary DEX directory */
    if (dex_load_file (full_path, &g_loaded_dexes[g_dex_count])) {
    
        printf ("[VM] Dynamically loaded %s\n", full_path);
        
        g_dex_count++;
        return 1;
    
    }

    /**
     * 2. Fallback: Try loading from the current working directory
     * 
     * Only attempt if the full_path was actually different from the fallback_name
     */
    if (strcmp (full_path, fallback_name) != 0) {
    
        if (dex_load_file (fallback_name, &g_loaded_dexes[g_dex_count])) {
        
            printf ("[VM] Dynamically loaded %s (fallback)\n", fallback_name);
            
            g_dex_count++;
            return 1;
        
        }
    
    }

    return 0;
}

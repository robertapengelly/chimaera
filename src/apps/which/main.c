/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <sys/stat.h>
#include    <unistd.h>

static int is_file (const char *path) {

    struct stat st;
    int ret;
    
    if ((ret = access (path, F_OK))) {
        return 0;
    }
    
    ret = stat (path, &st);
    
    if (ret) {
        return 0;
    }
    
    return S_ISREG (st.st_mode);

}

static int find_command (char *name) {

    char *path, *p, *file;
    int found;
    
    size_t len, name_len;
    
    if (!*name) {
        return 1;
    }
    
    if (strchr (name, '/') || strchr (name, '\\')) {
    
        if (!is_file (name)) {
            return 1;
        }
        
        printf ("%s\n", name);
        return 0;
    
    }
    
    if (!(path = getenv ("PATH"))) {
        return 1;
    }
    
    name_len = strlen (name);
    
    for (;;) {
    
        p = path;
        
        while (*p && *p != ':') {
            p++;
        }
        
        len = (size_t) (p - path);
        
        if (!(file = malloc ((len ? len : 1) + name_len + 2))) {
            return 1;
        }
        
        if (len) {
            memcpy (file, path, len);
        } else {
        
            file[0] = '.';
            len = 1;
        
        }
        
        file[len] = '/';
        strcpy (file + len + 1, name);
        
        if ((found = is_file (file))) {
            printf ("%s\n", file);
        }
        
        free (file);
        
        if (found) {
            return 0;
        }
        
        if (!*p) {
            break;
        }
        
        path = p + 1;
    
    }
    
    return 1;

}

/******************************************************************************
 * @function        main
 *****************************************************************************/
int main (int argc, char **argv) {

    int i, ret = 0;
    
    if (argc < 2) {
    
        fprintf (stderr, "usage: which command ...\n");
        return 1;
    
    }
    
    for (i = 1; i < argc; i++) {
    
        if (find_command (argv[i])) {
            ret = 1;
        }
    
    }
    
    return ret;

}

/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <errno.h>
#include    <stdio.h>
#include    <string.h>
#include    <unistd.h>

/******************************************************************************
 * @function        main
 *****************************************************************************/
int main (int argc, char **argv) {

    int i, start = 1, ret = 0;
    
    if (argc > 1 && !strcmp (argv[1], "--")) {
        start++;
    } else {
    
        for (i = 1; i < argc; i++) {
        
            if (argv[i][0] == '-' && argv[i][1]) {
            
                fprintf (stderr, "rmdir: unsupported option '%s'\n", argv[i]);
                return 1;
            
            }
        
        }
    
    }
    
    if (argc <= start) {
    
        fprintf (stderr, "usage: rmdir [--] directory ...\n");
        return 1;
    
    }
    
    for (i = start; i < argc; i++) {
    
        if (rmdir (argv[i]) < 0) {
        
            fprintf (stderr, "rmdir: cannot remove '%s' (errno %d)\n", argv[i], errno);
            ret = 1;
        
        }
    
    }
    
    return ret;

}

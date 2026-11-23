/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <sys/stat.h>
#include    <errno.h>
#include    <stdio.h>

/******************************************************************************
 * @function        main
 *****************************************************************************/
int main (int argc, char **argv) {

    int i, ret = 0;
    
    if (argc < 2) {
    
        fprintf (stderr, "usage: mkdir directory ...\n");
        return 1;
    
    }
    
    for (i = 1; i < argc; i++) {
    
        if (mkdir (argv[i], 0777) < 0) {
        
            fprintf (stderr, "mkdir: cannot create '%s' (errno %d)\n", argv[i], errno);
            ret = 1;
        
        }
    
    }
    
    return ret;

}

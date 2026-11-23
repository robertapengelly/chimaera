/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <fcntl.h>
#include    <stdio.h>
#include    <unistd.h>
#include    <utime.h>

/******************************************************************************
 * @function        main
 *****************************************************************************/
int main (int argc, char **argv) {

    int i, fd, ret = 0;
    
    if (argc < 2) {
    
        fprintf (stderr, "usage: touch file ...\n");
        return 1;
    
    }
    
    for (i = 1; i < argc; i++) {
    
        if (access (argv[i], F_OK) == 0) {
        
            if (utime (argv[i], 0) < 0) {
            
                fprintf (stderr, "touch: cannot update '%s'\n", argv[i]);
                ret = 1;
            
            }
            
            continue;
        
        }
        
        if ((fd = creat (argv[i], 0666)) < 0) {
        
            fprintf (stderr, "touch: cannot create '%s'\n", argv[i]);
            
            ret = 1;
            continue;
        
        }
        
        if (close (fd) < 0) {
        
            fprintf (stderr, "touch: cannot close '%s'\n", argv[i]);
            ret = 1;
        
        }
    
    }
    
    return ret;

}

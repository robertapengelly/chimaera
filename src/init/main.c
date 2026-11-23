/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <sys/wait.h>
#include    <fcntl.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <time.h>
#include    <unistd.h>

int main (int argc, char **argv) {

    int pid, wstatus;
    
    (void) argc;
    (void) argv;
    
    for (;;) {
    
        if (!(pid = fork ())) {
        
            char *argv[2] = { "/bin/sh", NULL };
            
            if (setenv ("PATH", "/bin:/sbin:/usr/bin", 0)) {
                /* Not sure if we need error handling?? */
                printf ("???\n");
            }
            
            execve ("/bin/sh", argv, __environ);
            
            /* Print a message. */
            printf ("failed to start /bin/sh\n");
            
            /* Exit with code 1. */
            exit (1);
        
        }
        
        wait (&wstatus);
    
    }

}

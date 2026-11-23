/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <stdint.h>
#include    <stdio.h>
#include    <string.h>

/******************************************************************************
 * @function        main
 *****************************************************************************/
int main (int argc, char **argv) {

    FILE *ifp, *ofp;
    
    uint8_t buffer[512];
    int ret = 0;
    
    size_t count;
    
    if (argc != 3) {
    
        fprintf (stderr, "usage: cp source dest\n");
        return 1;
    
    }
    
    if (strcmp (argv[1], argv[2]) == 0) {
    
        fprintf (stderr, "cannot copy '%s' onto itself\n", argv[1]);
        return 1;
    
    }
    
    if (!(ifp = fopen (argv[1], "rb"))) {
    
        fprintf (stderr, "cannot open '%s' for reading\n", argv[1]);
        return 1;
    
    }
    
    if (!(ofp = fopen (argv[2], "wb"))) {
    
        fprintf (stderr, "cannot open '%s' for writing\n", argv[2]);
        
        fclose (ifp);
        return 1;
    
    }
    
    for (;;) {
    
        count = fread (buffer, 1, sizeof (buffer), ifp);
        
        if (count && fwrite (buffer, 1, count, ofp) != count) {
        
            fprintf (stderr, "failed to write %llu bytes to '%s'\n", (unsigned long long) count, argv[2]);
            
            ret = 1;
            break;
        
        }
        
        if (ferror (ifp)) {
        
            fprintf (stderr, "failed to read from '%s'\n", argv[1]);
            
            ret = 1;
            break;
        
        }
        
        if (feof (ifp)) {
            break;
        }
    
    }
    
    if (fclose (ofp) == EOF) {
    
        fprintf (stderr, "failed to close '%s'\n", argv[2]);
        ret = 1;
    
    }
    
    fclose (ifp);
    return ret;

}

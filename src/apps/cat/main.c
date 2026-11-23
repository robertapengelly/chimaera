/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <stdio.h>
#include    "ll.h"

/******************************************************************************
 * @function        main
 *****************************************************************************/
int main (int argc, char **argv) {

    int ch, i;
    
    if (argc < 2) {
    
        while ((ch = getchar ()) != EOF) {
        
            if (printf ("%c", ch) < 0) {
                return 1;
            }
        
        }
        
        return 0;
    
    }
    
    for (i = 1; i < argc; i++) {
    
        FILE *fp;
        
        char *line, *line_end, *real_line;
        unsigned long real_line_len;
        
        unsigned long new_line_number = 1;
        unsigned long newlines;
        
        void *load_line_internal_data = NULL;
        
        if (!(fp = fopen (argv[i], "r"))) {
            continue;
        }
        
        load_line_internal_data = load_line_create_internal_data (&new_line_number);
        
        while (!load_line (&line, &line_end, &real_line, &real_line_len, &newlines, fp, &load_line_internal_data)) {
            printf ("%s", line);
        }
        
        load_line_destroy_internal_data (load_line_internal_data);
        fclose (fp);
    
    }
    
    return 0;

}

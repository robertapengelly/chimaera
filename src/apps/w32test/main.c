#include    <stdio.h>
#include    <stdlib.h>

#include    "ll.h"

int main (int argc, char **argv) {

    char *temp;
    
    if ((temp = getenv ("WIN32"))) {
        printf ("WIN32=%s\n", temp);
    }
    
    printf ("Hello, world!\n");

    for (int i = 0; i < argc; i++) {
        printf ("argument %d = %s\n", i, argv[i]);
    }

    if (argc == 2) {
    
        FILE *fp;
    
        char *line, *line_end, *real_line;
        unsigned long real_line_len;
    
        unsigned long new_line_number = 1;
        unsigned long newlines;
    
        void *load_line_internal_data = NULL;
    
        if (!(fp = fopen (argv[1], "r"))) {
            return 1;
    
        }
    
        load_line_internal_data = load_line_create_internal_data (&new_line_number);
    
        while (!load_line (&line, &line_end, &real_line, &real_line_len, &newlines, fp, &load_line_internal_data)) {
            printf ("%s", line);
        }
    
        load_line_destroy_internal_data (load_line_internal_data);
    
    }

    return 0;

}

/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <errno.h>
#include    <fcntl.h>
#include    <stdio.h>
#include    <string.h>
#include    <unistd.h>

#define     WC_LINES                    1
#define     WC_WORDS                    2
#define     WC_BYTES                    4

static int count_file (int fd, unsigned long *counts) {

    unsigned char buffer[512];
    
    ssize_t len, i;
    int in_word = 0;
    
    counts[0] = counts[1] = counts[2] = 0;
    
    while (1) {
    
        len = read (fd, buffer, sizeof (buffer));
        
        if (len < 0) {
        
            if (errno == EINTR) {
                continue;
            }
            
            return -1;
        
        }
        
        if (!len) {
            return 0;
        }
        
        counts[2] += (unsigned long) len;
        
        for (i = 0; i < len; i++) {
        
            int ch = buffer[i];
            
            if (ch == '\n') {
                counts[0]++;
            }
            
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v') {
                in_word = 0;
            } else if (!in_word) {
            
                counts[1]++;
                in_word = 1;
            
            }
        
        }
    
    }

}

static void print_counts (unsigned long *counts, int flags, const char *name) {

    const char *sep = "";
    int i;
    
    for (i = 0; i < 3; i++) {
    
        if (flags & (1 << i)) {
        
            printf ("%s%lu", sep, counts[i]);
            sep = " ";
        
        }
    
    }
    
    if (name) {
        printf (" %s", name);
    }
    
    printf ("\n");

}

int main (int argc, char **argv) {

    unsigned long counts[3], total[3] = { 0 };
    
    int flags = 0, arg = 1, status = 0;
    int fd, i;
    
    char *p;
    
    while (arg < argc && argv[arg][0] == '-' && argv[arg][1]) {
    
        if (!strcmp (argv[arg], "--")) {
        
            arg++;
            break;
        
        }
        
        for (p = argv[arg] + 1; *p; p++) {
        
            switch (*p) {
            
                case 'l':
                    flags |= WC_LINES;
                    break;
                
                case 'w':
                    flags |= WC_WORDS;
                    break;
                
                case 'c':
                    flags |= WC_BYTES;
                    break;
                
                default:
                    fprintf (stderr, "usage: wc [-lwc] [file ...]\n");
                    return 1;
            
            }
        
        }
        
        arg++;
    
    }
    
    if (!flags) {
        flags = WC_LINES | WC_WORDS | WC_BYTES;
    }
    
    if (arg == argc) {
    
        if (count_file (STDIN_FILENO, counts) < 0) {
        
            fprintf (stderr, "wc: standard input: read error\n");
            return 1;
        
        }
        
        print_counts (counts, flags, NULL);
        return 0;
    
    }
    
    for (i = arg; i < argc; i++) {
    
        fd = !strcmp (argv[i], "-") ? STDIN_FILENO : open (argv[i], O_RDONLY);
        
        if (fd < 0) {
        
            fprintf (stderr, "wc: %s: open error\n", argv[i]);
            status = 1;
            continue;
        
        }
        
        if (count_file (fd, counts) < 0) {
        
            fprintf (stderr, "wc: %s: read error\n", argv[i]);
            status = 1;
        
        } else {
        
            total[0] += counts[0];
            total[1] += counts[1];
            total[2] += counts[2];
            
            print_counts (counts, flags, argv[i]);
        
        }
        
        if (fd != STDIN_FILENO) {
            close (fd);
        }
    
    }
    
    if (argc - arg > 1) {
        print_counts (total, flags, "total");
    }
    
    return status;

}

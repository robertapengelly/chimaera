/******************************************************************************
 * @file            lib.c
 *****************************************************************************/
#include    <assert.h>
#include    <ctype.h>
#include    <limits.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "lib.h"
#include    "report.h"
#include    "unzip.h"
#include    "vector.h"

#define     OPTION_COMMENT              0x0001
#define     OPTION_DIRECTORY            0x0002
#define     OPTION_EXCLUDE              0x0003
#define     OPTION_HELP                 0x0004
#define     OPTION_LIST                 0x0005

struct option {

    const char *name;
    int index, flags;

};

#define     OPTION_NO_ARG               0x0001
#define     OPTION_HAS_ARG              0x0002

static struct option opts[] = {

    {   "--directory",      OPTION_DIRECTORY,       OPTION_HAS_ARG      },
    {   "-d",               OPTION_DIRECTORY,       OPTION_HAS_ARG      },
    
    {   "--comment",        OPTION_COMMENT,         OPTION_NO_ARG       },
    {   "-z",               OPTION_COMMENT,         OPTION_NO_ARG       },
    
    {   "--exclude",        OPTION_EXCLUDE,         OPTION_NO_ARG       },
    {   "-x",               OPTION_EXCLUDE,         OPTION_NO_ARG       },
    
    {   "--list",           OPTION_LIST,            OPTION_NO_ARG       },
    {   "-l",               OPTION_LIST,            OPTION_NO_ARG       },
    
    {   "--help",           OPTION_HELP,            OPTION_NO_ARG       },
    {   0,                  0,                      0                   }

};

static int strstart (const char *val, const char **str) {

    const char *p = val;
    const char *q = *str;
    
    while (*p != '\0') {
    
        if (*p != *q) {
            return 0;
        }
        
        ++p;
        ++q;
    
    }
    
    *str = q;
    return 1;

}

static void print_help (void) {

    if (program_name) {
    
        fprintf (stderr, "Usage: %s [opts] file... [-x xlist]\n\n", program_name);
        fprintf (stderr, "Options:\n\n");
        
        fprintf (stderr, "    -l     List files     -z     Display archive comment only.\n");
        fprintf (stderr, "\n");
        
        fprintf (stderr, "    -d exdir              Extract files into exdir.\n");
        fprintf (stderr, "    -x xlist              Exclude files that follow.\n");
        fprintf (stderr, "\n");
        
        fprintf (stderr, "    --help                Show this help information then exit.\n");
    
    }
    
    exit (EXIT_SUCCESS);

}

static void dynarray_add (void *ptab, long *nb_ptr, void *data) {

    int nb, nb_alloc;
    void **pp;
    
    nb = *nb_ptr;
    pp = *(void ***) ptab;
    
    if ((nb & (nb - 1)) == 0) {
    
        if (!nb) {
            nb_alloc = 1;
        } else {
            nb_alloc = nb * 2;
        }
        
        pp = xrealloc (pp, nb_alloc * sizeof (void *));
        *(void ***) ptab = pp;
    
    }
    
    pp[nb++] = data;
    *nb_ptr = nb;

}

uint64_t array_to_integer (unsigned char *arr, int size, int bigendian) {

    uint64_t val = 0;
    int i;
    
    if (bigendian) {
    
        int j;
        
        for (i = size, j = 0; i > 0; i--, j++) {
            val |= (uint64_t) arr[j] << (CHAR_BIT * (i - 1));
        }
    
    } else {
    
        for (i = 0; i < size; i++) {
            val |= (uint64_t) arr[i] << (CHAR_BIT * i);
        }
    
    }
    
    return val;

}

int lsb (uint64_t x, uint64_t n) {

    assert (n <= 63);
    return x & (((uint64_t) 1 << n) - 1);

}

uint64_t round_up (uint64_t x, uint64_t m) {

    assert ((m & (m - 1)) == 0 && "m must be a power of two");
    return (x + m - 1) & (uint64_t) (-m);

}

int wild_compare (const char *wild, const char *s) {

    const char *cp = 0, *mp = 0;
    
    while (*s && (*wild != '*')) {
    
        if (*wild != *s) {
            return 0;
        }
        
        wild++;
        s++;
    
    }
    
    while (*s) {
    
        if (*wild == '*') {
        
            if (!*++wild) {
                return 1;
            }
            
            mp = wild;
            cp = s + 1;
        
        } else if (*wild == *s || *wild == '?') {
        
            wild++;
            s++;
        
        } else {
        
            wild = mp;
            s = cp++;
        
        }
    
    }
    
    while (*wild == '*') {
        wild++;
    }
    
    return !*wild;

}

void parse_args (int argc, char **argv, int optind) {

    struct option *popt;
    const char *optarg, *r;
    
    if (argc <= optind) {
        print_help ();
    }
    
    while (optind < argc) {
    
        r = argv[optind++];
        
        if (r[0] != '-' || r[1] == '\0') {
        
            dynarray_add (&state->files, &state->nb_files, xstrdup (r));
            continue;
        
        }
        
        for (popt = opts; popt; popt++) {
        
            const char *p1 = popt->name;
            const char *r1 = r;
            
            if (!p1) {
            
                report_at (program_name, 0, REPORT_ERROR, "invalid option -- '%s'", r);
                exit (EXIT_FAILURE);
            
            }
            
            if (!strstart (p1, &r1)) {
                continue;
            }
            
            optarg = r1;
            
            if (popt->flags & OPTION_HAS_ARG) {
            
                if (*optarg == '\0') {
                
                    if (optind >= argc) {
                    
                        report_at (program_name, 0, REPORT_ERROR, "argument to '%s' is missing", r);
                        exit (EXIT_FAILURE);
                    
                    }
                    
                    optarg = argv[optind++];
                
                }
            
            } else if (*optarg != '\0') {
                continue;
            }
            
            break;
        
        }
        
        switch (popt->index) {
        
            case OPTION_COMMENT: {
            
                state->only_comment = 1;
                break;
            
            }
            
            case OPTION_DIRECTORY: {
            
                uint64_t len, i;
                
                if (state->exdir) {
                
                    report_at (program_name, 0, REPORT_ERROR, "-d option used more than once (only one exdir allowed)");
                    exit (EXIT_FAILURE);
                
                }
                
                len = strlen (state->exdir = xstrdup (optarg));
                
                for (i = 0; i < len; i++) {
                
                    if (state->exdir[i] == '\\') {
                        ((char *) state->exdir)[i] = '/';
                    }
                
                }
                
                break;
            
            }
            
            case OPTION_EXCLUDE: {
            
                char *arg, *copy, *p;
                int i;
                
                for (; optind < argc; optind++) {
                
                    optarg = argv[optind++];
                    
                    if (!optarg || !*optarg) {
                        continue;
                    }
                    
                    arg = (copy = xstrdup (optarg));
                    
                    while (arg && *arg != '\0') {
                    
                        if (isspace ((int) *arg)) {
                        
                            while (*arg != '\0') {
                            
                                if (!isspace ((int) *arg)) {
                                    break;
                                }
                                
                                arg++;
                            
                            }
                            
                            continue;
                        
                        }
                        
                        if ((p = strchr (arg, ' '))) {
                            *p++ = '\0';
                        }
                        
                        for (i = 0; i < state->xlist.length; i++) {
                        
                            if (strcmp (state->xlist.data[i], arg) == 0) {
                                break;
                            }
                        
                        }
                        
                        if (i < state->xlist.length) {
                        
                            arg = p;
                            continue;
                        
                        }
                        
                        vec_push (&state->xlist, xstrdup (arg));
                        arg = p;
                    
                    }
                    
                    free (copy);
                
                }
                
                break;
            
            }
            
            case OPTION_HELP: {
            
                print_help ();
                break;
            
            }
            
            case OPTION_LIST: {
            
                state->list = 1;
                break;
            
            }
            
            default: {
            
                report_at (program_name, 0, REPORT_ERROR, "unsupported option '%s'", r);
                exit (EXIT_FAILURE);
            
            }
        
        }
    
    }

}

char *xstrdup (const char *str) {

    char *ptr = xmalloc (strlen (str) + 1);
    strcpy (ptr, str);
    
    return ptr;

}

void *xmalloc (unsigned long size) {

    void *ptr = malloc (size);
    
    if (ptr == NULL && size) {
    
        report_at (program_name, 0, REPORT_ERROR, "memory full (malloc)");
        exit (EXIT_FAILURE);
    
    }
    
    memset (ptr, 0, size);
    return ptr;

}

void *xrealloc (void *ptr, unsigned long size) {

    void *new_ptr = realloc (ptr, size);
    
    if (new_ptr == NULL && size) {
    
        report_at (program_name, 0, REPORT_ERROR, "memory full (realloc)");
        exit (EXIT_FAILURE);
    
    }
    
    return new_ptr;

}

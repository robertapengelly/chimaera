/******************************************************************************
 * @file            lib.c
 *****************************************************************************/
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "lib.h"
#include    "ls.h"
#include    "report.h"

struct option {

    const char *name;
    int index, flags;

};

#define     OPTION_NO_ARG               0x0001
#define     OPTION_HAS_ARG              0x0002

enum options {

    OPTION_IGNORED = 0,
    OPTION_HELP,
    OPTION_LIST_ALL,
    OPTION_LONG_LISTING

};

static struct option opts[] = {

    {   "--all",                    OPTION_LIST_ALL,        OPTION_NO_ARG   },
    {   "--help",                   OPTION_HELP,            OPTION_NO_ARG   },
    {   "--long",                   OPTION_LONG_LISTING,    OPTION_NO_ARG   },
    {   0,                          0,                      0               }

};

static char *format_path (char *__str) {

    unsigned long len, i;
    char *p = __str;
    
    len = strlen (p);
    
    for (i = 0; i < len; i++) {
    
        if (p[i] == '\\') {
            p[i] = '/';
        }
        
        if (p[i] == '/') {
        
            i++;
            
            while (p[i] == '/' || p[i] == '\\') {
                memmove (p + i, p + i + 1, strlen (p + i));
            }
        
        }
    
    }
    
    return xstrdup (p);

}

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

static void print_help (int exitval) {

    if (!program_name) {
        goto _exit;
    }
    
    fprintf (stderr, "Usage: %s [option]... [file]...\n\n", program_name);
    fprintf (stderr, "Options:\n\n");
    fprintf (stderr, "    -l, --long                    Display detailed information.\n");
    fprintf (stderr, "    -a, --all                     Do not ignore hidden files.\n");
    fprintf (stderr, "\n");
    fprintf (stderr, "    --help                        Print help information.\n");
    fprintf (stderr, "\n");
    
_exit:
    
    exit (exitval);

}

char *xstrdup (const char *__str) {

    char *ptr = xmalloc (strlen (__str) + 1);
    strcpy (ptr, __str);
    
    return ptr;

}

void dynarray_add (void *ptab, long *nb_ptr, void *data) {

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

void parse_args (int argc, char **argv, int optind) {

    const char *optarg, *r;
    struct option *popt;
    
    while (optind < argc) {
    
        r = argv[optind++];
        
        if (r[0] != '-' || r[1] == '\0') {
        
            char *p = xstrdup (r);
            
            dynarray_add (&state->paths, &state->nb_paths, format_path (p));
            free (p);
            
            continue;
        
        }
        
        if (r[1] == '-') {
        
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
            
                case OPTION_HELP: {
                
                    print_help (0);
                    break;
                
                }
                
                case OPTION_LIST_ALL: {
                
                    state->list_hidden = 1;
                    break;
                
                }
                
                case OPTION_LONG_LISTING: {
                
                    state->long_listing = 1;
                    break;
                
                }
                
                default: {
                
                    report_at (program_name, 0, REPORT_ERROR, "unsupported option '%s'", r);
                    exit (EXIT_FAILURE);
                
                }
            
            }
            
            continue;
        
        }
        
        r++;
        
        while (*r != '\0') {
        
            switch (*r) {
            
                case 'a':
                
                    state->list_hidden = 1;
                    
                    r++;
                    break;
                
                case 'l':
                
                    state->long_listing = 1;
                    
                    r++;
                    break;
                
                default:
                
                    report_at (program_name, 0, REPORT_ERROR, "unsupported option '%c'", *r);
                    exit (EXIT_FAILURE);
            
            }
        
        }
    
    }

}

void *xmalloc (unsigned long __size) {

    void *p = malloc (__size);
    
    if (!p && __size) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to allocate %ld bytes of memory", __size);
        exit (EXIT_FAILURE);
    
    }
    
    memset (p, 0, __size);
    return p;

}

void *xrealloc (void *__ptr, unsigned long __size) {

    void *p = realloc (__ptr, __size);
    
    if (!p && __size) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to allocate %ld bytes of memory", __size);
        exit (EXIT_FAILURE);
    
    }
    
    return p;

}

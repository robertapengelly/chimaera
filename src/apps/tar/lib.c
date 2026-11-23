/******************************************************************************
 * @file            lib.c
 *****************************************************************************/
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "lib.h"
#include    "report.h"
#include    "tar.h"

struct option {

    const char *name;
    int index, flags;

};

#define     OPTION_NO_ARG               0x0001
#define     OPTION_HAS_ARG              0x0002

enum options {

    OPTION_IGNORED = 0,
    OPTION_APPEND,
    OPTION_CREATE,
    OPTION_EXTRACT,
    OPTION_FILE,
    OPTION_HELP,
    OPTION_MODE,
    OPTION_ROOT

};

static struct option opts[] = {

    {   "-C",                       OPTION_ROOT,            OPTION_HAS_ARG  },
    
    {   "-c",                       OPTION_CREATE,          OPTION_NO_ARG   },
    {   "-f",                       OPTION_FILE,            OPTION_HAS_ARG  },
    {   "-r",                       OPTION_APPEND,          OPTION_NO_ARG   },
    {   "-x",                       OPTION_EXTRACT,         OPTION_NO_ARG   },
    
    {   "--append",                 OPTION_APPEND,          OPTION_NO_ARG   },
    {   "--create",                 OPTION_CREATE,          OPTION_NO_ARG   },
    {   "--extract",                OPTION_EXTRACT,         OPTION_NO_ARG   },
    {   "--file",                   OPTION_FILE,            OPTION_HAS_ARG  },
    {   "--help",                   OPTION_HELP,            OPTION_NO_ARG   },
    {   "--mode",                   OPTION_MODE,            OPTION_HAS_ARG  },
    
    {   "--directory",              OPTION_ROOT,            OPTION_HAS_ARG  },
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
    
    fprintf (stderr, "Usage: %s [options] [file]...\n\n", program_name);
    fprintf (stderr, "Options:\n\n");
    fprintf (stderr, "    -c, --create                  Create a new archive.\n");
    /*fprintf (stderr, "    -r, --append                  Append files to the end of the archive.\n");*/
    fprintf (stderr, "    -x, --extract                 Extract files from an archive.\n");
    fprintf (stderr, "\n");
    fprintf (stderr, "    -f ARCHIVE, --file ARCHIVE    Use archive file or device ARCHIVE.\n");
    fprintf (stderr, "    --help                        Print this help information.\n");
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
    
    if (optind >= argc) {
        print_help (EXIT_FAILURE);
    }
    
    while (optind < argc) {
    
        r = argv[optind++];
        
        if (r[0] != '-' || r[1] == '\0') {
        
            char *p = xstrdup (r);
            
            dynarray_add (&state->paths, &state->nb_paths, format_path (p));
            free (p);
            
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
        
            case OPTION_APPEND: {
            
                state->append = 1;
                break;
            
            }
            
            case OPTION_CREATE: {
            
                state->create = 1;
                break;
            
            }
            
            case OPTION_EXTRACT: {
            
                state->extract = 1;
                break;
            
            }
            
            case OPTION_FILE: {
            
                if (state->target) {
                
                    report_at (program_name, 0, REPORT_ERROR, "multiple archive files provided");
                    exit (EXIT_FAILURE);
                
                }
                
                state->target = xstrdup (optarg);
                break;
            
            }
            
            case OPTION_HELP: {
            
                print_help (EXIT_SUCCESS);
                break;
            
            }
            
            case OPTION_MODE: {
            
                if (strlen (optarg) != 3 && strlen (optarg) != 4) {
                
                    report_at (program_name, 0, REPORT_ERROR, "invalid mode provided");
                    exit (EXIT_FAILURE);
                
                }
                
                if (state->mode) { free (state->mode); }
                state->mode = xmalloc (5);
                
                if (strlen (optarg) == 3) {
                
                    state->mode[0] = '0';
                    memcpy (state->mode + 1, optarg, 3);
                
                } else {
                    memcpy (state->mode, optarg, 4);
                }
                
                break;
            
            }
            
            case OPTION_ROOT: {
            
                char *p = xstrdup (optarg);
                
                if (state->root) { free (state->root); }
                state->root = format_path (p);
                
                free (p);
                break;
            
            }
            
            default: {
            
                report_at (program_name, 0, REPORT_ERROR, "unsupported option '%s'", r);
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

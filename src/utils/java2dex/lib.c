/******************************************************************************
 * @file            lib.c
 *****************************************************************************/
#include    <ctype.h>
#include    <stdarg.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "j2d.h"
#include    "lex.h"
#include    "lib.h"
#include    "report.h"

struct j2d_option {

    const char *name;
    int idx, flags;

};

#define     J2D_OPTION_NO_ARG           0
#define     J2D_OPTION_HAS_ARG          1
#define     J2D_OPTION_EQUALS_ARG       3

#define     J2D_OPTION_NONE             0
#define     J2D_OPTION_HELP             1
#define     J2D_OPTION_MAX_ERRORS       2
#define     J2D_OPTION_OUTFILE          3

static struct j2d_option opts[] = {

    {   "-o",                               J2D_OPTION_OUTFILE,                     J2D_OPTION_HAS_ARG      },
    
    {   "-fmax-errors",                     J2D_OPTION_MAX_ERRORS,                  J2D_OPTION_EQUALS_ARG   },
    {   "--help",                           J2D_OPTION_HELP,                        J2D_OPTION_NO_ARG       },
    
    {   0,                                  0,                                      0                       }

};


static int strstart (const char *val, const char **str) {

    const char *p = *str;
    const char *q = val;
    
    while (*q != '\0') {
    
        if (*p != *q) {
            return 0;
        }
        
        ++p;
        ++q;
    
    }
    
    *str = p;
    return 1;

}

static void print_usage (void) {

    if (program_name) {
    
        fprintf (stderr, "Usage: %s [options] file...\n\n", program_name);
        fprintf (stderr, "Options:\n\n");
        
        fprintf (stderr, "    -fmax-errors=<count>              Stop execution after <count> errros.\n");
        fprintf (stderr, "    -mlong64                          Convert long to 64-bit.\n");
        fprintf (stderr, "    -o OBJFILE                        Name the object-file output OBJFILE.\n");
        fprintf (stderr, "\n");
        fprintf (stderr, "    --help                            Print this help information.\n");
        
        fprintf (stderr, "\n");
    
    }

}


char *skip_whitespace (char *__p) {

    while (*__p == ' ' || *__p == '\t') {
        __p++;
    }
    
    return __p;

}

char *symname (char **pp) {

    char *p = *pp;
    
    if (is_name_beginner ((int) **pp)) {
    
        while (is_name_part ((int) **pp)) {
            (*pp)++;
        }
        
        return xstrndup (p, *pp - p);
    
    }
    
    return 0;

}

char *xstrdup (const char *__p) {

    char *p = xmalloc (strlen (__p) + 1);
    
    strcpy (p, __p);
    return p;

}

char *xstrndup (const char *__p, unsigned long __len) {

    char *p = xmalloc (__len + 1);
    
    strncpy (p, __p, __len);
    return p;

}

void dynarray_add (void *ptab, long *nb_ptr, void *data) {

    long nb, nb_alloc;
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

    struct j2d_option *popt;
    const char *optarg, *r;
    
    if (argc <= optind) {
    
        print_usage ();
        exit (EXIT_SUCCESS);
    
    }
    
    while (optind < argc) {
    
        r = argv[optind++];
        
        if (r[0] != '-' || r[1] == '\0') {
        
            dynarray_add (&state->input_files, &state->nb_input_files, xstrdup (r));
            continue;
        
        }
        
        for (popt = opts; ; popt++) {
        
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
            
            if (popt->flags & J2D_OPTION_HAS_ARG) {
            
                if (!(popt->flags & ~J2D_OPTION_HAS_ARG)) {
                
                    if (*r1 == '\0') {
                    
                        if (optind >= argc) {
                        
                            report_at (program_name, 0, REPORT_ERROR, "argument to '%s' is missing", r);
                            exit (EXIT_FAILURE);
                        
                        }
                        
                        optarg = argv[optind++];
                    
                    }
                
                } else {
                
                    if (*r1 != '=') {
                    
                        report_at (program_name, 0, REPORT_ERROR, "argument to '%s' is missing", r);
                        exit (EXIT_FAILURE);
                    
                    }
                    
                    optarg++;
                
                }
            
            } else if (*r1 != '\0') {
                continue;
            }
            
            break;
        
        }
        
        switch (popt->idx) {
        
            case J2D_OPTION_HELP: {
            
                print_usage ();
                exit (EXIT_SUCCESS);
            
            }
            
            case J2D_OPTION_MAX_ERRORS: {
            
                if (!(state->max_errors = atol (optarg))) {
                
                    report_at (program_name, 0, REPORT_ERROR, "invalid number for -fmax-errors=");
                    exit (EXIT_FAILURE);    
                
                }
                
                break;
            
            }
            
            case J2D_OPTION_OUTFILE: {
            
                if (state->ofile) {
                
                    report_at (program_name, 0, REPORT_ERROR, "multiple output files provided");
                    exit (EXIT_FAILURE);
                
                }
                
                state->ofile = xstrdup (optarg);
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

    void *ptr = malloc (__size);
    
    if (!ptr && __size) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to allocate memory");
        exit (EXIT_FAILURE);
    
    }
    
    memset (ptr, 0, __size);
    return ptr;

}

void *xrealloc (void *__ptr, unsigned long __size) {

    void *ptr = realloc (__ptr, __size);
    
    if (!ptr && __size) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to reallocate memory)");
        exit (EXIT_FAILURE);
    
    }
    
    return ptr;

}


static const char *filename = 0;
static unsigned long line_number = 0;

const char *get_filename (void) {
    return filename;
}

unsigned long get_line_number (void) {
    return line_number;
}

void get_filename_and_line_number (const char **__filename_p, unsigned long *__line_number_p) {

    *__filename_p = filename;
    *__line_number_p = line_number;

}

void set_filename (const char *__filename) {
    filename = __filename;
}

void set_line_number (unsigned long __line_number) {
    line_number = __line_number;
}

void set_filename_and_line_number (const char *__filename, unsigned long __line_number) {

    filename = __filename;
    line_number = __line_number;

}


const char *real_filename = 0;
static unsigned long real_line_number = 0;

const char *get_real_filename (void) {
    return real_filename;
}

unsigned long get_real_line_number (void) {
    return real_line_number;
}

void set_real_filename (const char *__filename) {
    real_filename = __filename;
}

void set_real_line_number (unsigned long __line_number) {
    real_line_number = __line_number;
}

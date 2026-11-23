/******************************************************************************
 * @file            lib.c
 *****************************************************************************/
#include    <ctype.h>
#include    <stdlib.h>
#include    <string.h>

#include    "pkg.h"
#include    "lib.h"
#include    "report.h"

struct pkg_option {

    const char *name;
    int idx, flgs;

};

#define     PKG_OPTION_NO_ARG           0
#define     PKG_OPTION_HAS_ARG          1

#define     PKG_OPTION_NONE             0
#define     PKG_OPTION_HELP             1

static struct pkg_option opts[] = {

    {   "--help",       PKG_OPTION_HELP,        PKG_OPTION_NO_ARG       },
    {   0,              0,                      0                       }

};


static int _strstart (const char *val, const char **str) {

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

static void _print_usage (void) {

    if (program_name) {
    
        fprintf (stderr, "Usage: %s [options] file\n\n", program_name);
        fprintf (stderr, "Options:\n\n");
        
        /*fprintf (stderr, "\n");*/
        fprintf (stderr, "    --help                Print this help information.\n");
        
        fprintf (stderr, "\n");
    
    }

}

static void dynarray_add (void *ptab, long *nb_ptr, void *data) {

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


char *get_token (char **pp) {

    const char *temp = skip_whitespace (*pp), *q;
    int in_quotes = 0, ch = 0;
    
    for (;;) {
    
        if (**pp == '\'' || **pp == '"') {
        
            in_quotes = !in_quotes;
            
            (*pp)++;
            continue;
        
        }
        
        if (isspace ((int) **pp)) {
        
            if (!in_quotes) {
                break;
            }
        
        }
        
        (*pp)++;
    
    }
    
    if (*temp == '"' || *temp == '\'') {
    
        ch = *temp;
        
        while (*temp == '"' || *temp == '\'') {
            temp++;
        }
    
    }
    
    q = *pp;
    
    if (ch) {
    
        while (*(q - 1) == ch) {
            q--;
        }
    
    }
    
    return xstrndup (temp, q - temp);

}

char *skip_whitespace (char *__p) {

    while (*__p == ' ' || *__p == '\t') {
        __p++;
    }
    
    return __p;

}

char *xstrdup (const char *__p) {

    char *p = xmalloc (strlen (__p) + 1);
    
    strcpy (p, __p);
    return p;

}

char *xstrndup (const char *__p, unsigned long __len) {

    char *p = xmalloc (__len + 1);
    
    memcpy (p, __p, __len);
    return p;

}

int xstrcasecmp (const char *__s1, const char *__s2) {

    const unsigned char *p1 = (const unsigned char *) __s1;
    const unsigned char *p2 = (const unsigned char *) __s2;
    
    while (*p1 != '\0') {
    
        if (tolower ((int) *p1) < tolower ((int) *p2)) {
            return (-1);
        } else if (tolower ((int) *p1) > tolower ((int) *p2)) {
            return (1);
        }
        
        p1++;
        p2++;
    
    }
    
    if (*p2 == '\0') {
        return (0);
    }
    
    return (-1);

}

int xstrncasecmp (const char *__s1, const char *__s2, unsigned long __len) {

    const unsigned char *p1 = (const unsigned char *) __s1;
    const unsigned char *p2 = (const unsigned char *) __s2;
    
    while (*p1 != '\0' && __len-- > 0) {
    
        if (tolower ((int) *p1) < tolower ((int) *p2)) {
            return (-1);
        } else if (tolower ((int) *p1) > tolower ((int) *p2)) {
            return (1);
        }
        
        p1++;
        p2++;
    
    }
    
    if (*p2 == '\0') {
        return (0);
    }
    
    return (-1);

}

void ignore_rest_of_line (char **pp) {

    while (**pp != '\n' && **pp != '\0') {
        (*pp)++;
    }

}

void parse_args (int argc, char **argv, int optind) {

    struct pkg_option *popt;
    const char *r;
    
    if (argc <= optind) {
    
        _print_usage ();
        exit (EXIT_SUCCESS);
    
    }
    
    while (optind < argc) {
    
        r = argv[optind++];
        
        if (r[0] != '-' || r[1] == '\0') {
        
            dynarray_add (&state->args, &state->nb_args, xstrdup (r));
            continue;
        
        }
        
        for (popt = opts; ; popt++) {
        
            const char *p1 = popt->name;
            const char *r1 = r;
            
            if (!p1) {
            
                report_at (program_name, 0, REPORT_ERROR, "invalid option -- '%s'", r);
                exit (EXIT_FAILURE);
            
            }
            
            if (!_strstart (p1, &r1)) {
                continue;
            }
            
            if (*r1 != '\0') {
                continue;
            }
            
            break;
        
        }
        
        switch (popt->idx) {
        
            case PKG_OPTION_HELP: {
            
                _print_usage ();
                exit (EXIT_SUCCESS);
            
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
    
        report_at (program_name, 0, REPORT_ERROR, "memory full (malloc)");
        exit (EXIT_FAILURE);
    
    }
    
    memset (ptr, 0, __size);
    return ptr;

}

void *xrealloc (void *__ptr, unsigned long __size) {

    void *ptr = realloc (__ptr, __size);
    
    if (!ptr && __size) {
    
        report_at (program_name, 0, REPORT_ERROR, "memory full (realloc)");
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

void set_filename_and_line_number (const char *__filename, unsigned long __line_number) {

    filename = __filename;
    line_number = __line_number;

}

void set_line_number (unsigned long __line_number) {
    line_number = __line_number;
}

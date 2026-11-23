/******************************************************************************
 * @file            lib.c
 *****************************************************************************/
#include    <ctype.h>
#include    <stdlib.h>
#include    <string.h>

#include    "regf.h"
#include    "lib.h"
#include    "report.h"

struct regf_option {

    const char *name;
    int idx, flgs;

};

#define     REGF_OPTION_NO_ARG          0
#define     REGF_OPTION_HAS_ARG         1

#define     REGF_OPTION_NONE            0
#define     REGF_OPTION_HELP            1

#define     REGF_OPTION_GENERATE        2
#define     REGF_OPTION_SET             3

static struct regf_option opts[] = {

    {   "gen",          REGF_OPTION_GENERATE,   REGF_OPTION_NO_ARG      },
    {   "set",          REGF_OPTION_SET,        REGF_OPTION_NO_ARG      },
    
    {   "--help",       REGF_OPTION_HELP,       REGF_OPTION_NO_ARG      },
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


unsigned char *ascii_to_utf16le (char **pp, uint32_t *out_len) {

    unsigned char *p;
    char *tmp;
    
    size_t n, i;
    int ch = 0;
    
    *pp = skip_whitespace (*pp);
    
    if (**pp == '"' || **pp == '\'') {
    
        ch = **pp;
        (*pp)++;
    
    }
    
    tmp = *pp;
    
    for (;;) {
    
        if (**pp == '\n' || **pp == '\0') {
            break;
        }
        
        if (ch && **pp == ch) {
            break;
        }
        
        (*pp)++;
    
    }
    
    p = xmalloc (((n = (*pp - tmp)) + 1U) * 2U);
    
    for (i = 0; i < n; i++) {
    
        p[i * 2U + 0U] = tmp[i];
        p[i * 2U + 1U] = 0;
    
    }
    
    p[n * 2U + 0U] = 0;
    p[n * 2U + 1U] = 0;
    
    *out_len = (uint32_t) ((n + 1U) * 2U);
    
    if (ch) { (*pp)++; }
    return p;

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

    struct regf_option *popt;
    const char *optarg, *r;
    
    if (argc <= optind) {
    
        _print_usage ();
        exit (EXIT_SUCCESS);
    
    }
    
    while (optind < argc) {
    
        r = argv[optind++];
        
        for (popt = opts; ; popt++) {
        
            const char *p1 = popt->name;
            const char *r1 = r;
            
            if (!p1) {
            
                if (state->mode != MODE_NONE) {
                
                    optarg = r1;
                    
                    switch (state->mode) {
                    
                        case MODE_SET: {
                        
                            dynarray_add (&state->args, &state->nb_args, xstrdup (optarg));
                            goto next;
                        
                        }
                        
                        case MODE_GENERATE: {
                        
                            dynarray_add (&state->args, &state->nb_args, xstrdup (optarg));
                            goto next;
                        
                        }
                        
                        default: {
                            break;
                        }
                    
                    }
                
                }
                
                report_at (program_name, 0, REPORT_ERROR, "invalid option -- '%s'", r);
                exit (EXIT_FAILURE);
            
            }
            
            if (!_strstart (p1, &r1)) {
                continue;
            }
            
            optarg = r1;
            
            if (popt->flgs & REGF_OPTION_HAS_ARG) {
            
                if (*r1 == '\0') {
                
                    if (optind >= argc) {
                    
                        report_at (program_name, 0, REPORT_ERROR, "argument to '%s' is missing", r);
                        exit (EXIT_FAILURE);
                    
                    }
                    
                    optarg = argv[optind++];
                
                }
            
            } else if (*r1 != '\0') {
                continue;
            }
            
            break;
        
        }
        
        switch (popt->idx) {
        
            case REGF_OPTION_GENERATE: {
            
                if (state->mode != MODE_NONE) {
                
                    report_at (program_name, 0, REPORT_ERROR, "more than one mode specified '%s'", r);
                    exit (EXIT_FAILURE);
                
                }
                
                state->mode = MODE_GENERATE;
                
                if (argc != 4) {
                
                    report_at (program_name, 0, REPORT_ERROR, "invalid number of arguments for '%s'", r);
                    exit (EXIT_FAILURE);
                
                }
                
                break;
            
            }
            
            case REGF_OPTION_SET: {
            
                if (state->mode != MODE_NONE) {
                
                    report_at (program_name, 0, REPORT_ERROR, "more than one mode specified '%s'", r);
                    exit (EXIT_FAILURE);
                
                }
                
                state->mode = MODE_SET;
                
                if (argc != 7) {
                
                    report_at (program_name, 0, REPORT_ERROR, "invalid number of arguments for '%s'", r);
                    exit (EXIT_FAILURE);
                
                }
                
                break;
            
            }
            
            case REGF_OPTION_HELP: {
            
                _print_usage ();
                exit (EXIT_SUCCESS);
            
            }
            
            default: {
            
                report_at (program_name, 0, REPORT_ERROR, "unsupported option '%s'", r);
                exit (EXIT_FAILURE);
            
            }
        
        }
        
    next:
        
        continue;
    
    }
    
    if (!state->ofile) { state->ofile = "a.out"; }

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


uint32_t align8 (uint32_t x) {
    return (x + 7) & ~7;
}

#include    <limits.h>

static uint64_t array_to_integer (unsigned char *arr, int size) {

    uint64_t value = 0;
    int i;
    
    for (i = 0; i < size; i++) {
        value |= (uint64_t) arr[i] << (CHAR_BIT * i);
    }
    
    return value;

}

uint16_t rd16 (uint8_t *p) {
    return array_to_integer (p, 2);
}

uint32_t rd32 (uint8_t *p) {
    return array_to_integer (p, 4);
}

static void integer_to_array (unsigned char *dest, uint64_t value, int size) {

    int i;
    
    for (i = 0; i < size; i++) {
        dest[i] = (value >> (CHAR_BIT * i)) & UCHAR_MAX;
    }

}

void wr16 (uint8_t *p, uint16_t v) {
    integer_to_array (p, v, 2);
}

void wr32 (uint8_t *p, uint32_t v) {
    integer_to_array (p, v, 4);
}

void wr64 (uint8_t *p, uint64_t v) {
    integer_to_array (p, v, 8);
}


int ascii_casecmp (const char *a, const char *b) {

    unsigned char ca, cb;
    
    for (;;) {
    
        ca = *a++;
        cb = *b++;
        
        if (ca >= 'a' && ca <= 'z') {
            ca = (ca - 'a' + 'A');
        }
        
        if (cb >= 'a' && cb <= 'z') {
            cb = (cb - 'a' + 'A');
        }
        
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        
        if (ca == 0) {
            return 0;
        }
    
    }

}

uint32_t utf16le_len_bytes_from_ascii (const char *s) {
    return strlen (s) * 2;
}

#include    <time.h>

uint64_t unix_time_to_filetime (time_t t) {
    return ((uint64_t) t + 11644473600ULL) * 10000000ULL;
}

/******************************************************************************
 * @file            tree.c
 *****************************************************************************/
#include    <ctype.h>
#include    <errno.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "regf.h"
#include    "ll.h"
#include    "lib.h"
#include    "tree.h"

#include    "hashtab.h"
#include    "report.h"

static int hex_digit_val (int c) {

    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    
    return -1;

}

int parse_binary_hex (char **pp, unsigned char **out_data, uint32_t *out_len) {

    unsigned char *buf;
    
    uint32_t cap = 16, len = 0;
    int hi = -1;
    
    buf = xmalloc (cap);
    
    while (**pp) {
    
        int v;
        
        if (isspace ((int) **pp) || **pp == ',' || **pp == ':' || **pp == '-') {
        
            (*pp)++;
            continue;
        
        }
        
        v = hex_digit_val ((int) **pp);
        
        if (v < 0) {
            return -1;
        }
        
        if (hi < 0) {
            hi = v;
        } else {
        
            if (len == cap) {
                buf = xrealloc (buf, cap = ARRAY_GROW_CAP (cap));
            }
            
            buf[len++] = ((hi << 4) | v);
            hi = -1;
        
        }
        
        (*pp)++;
    
    }
    
    if (hi >= 0) {
        return hi;
    }
    
    *out_data = buf;
    *out_len = len;
    
    return 0;

}

int parse_u32_strict (const char *s, uint32_t *out) {

    char *end;
    unsigned long v;
    
    errno = 0;
    v = strtoul (s, &end, 0);
    
    if (errno || *s == 0 || *end != 0 || v > 0xFFFFFFFFUL) {
        return 0;
    }
    
    *out = v;
    return 1;

}

int parse_u64_strict (const char *s, uint64_t *out) {

    char *end;
    errno = 0;
    
#if defined (_MSC_VER)
    
    unsigned __int64 v;
    v = _strtoui64 (s, &end, 0);
    
    if (errno || *s == 0 || *end != 0) {
        return 0;
    }
    
    *out = v;
    
#else

    unsigned long long v;
    v = strtoul (s, &end, 0);
    
    if (errno || *s == 0 || *end != 0) {
        return 0;
    }
    
    *out = v;
    
#endif
    
    return 1;

}

struct command_entry {

    const char *name;
    void (*handler) (char *start, char **pp, rf_key_t *root);

};

static void handler_binary (char *start, char **pp, rf_key_t *root) {

    char *name, *path, *caret, *xx;
    int ret;
    
    unsigned char *data;
    uint32_t data_size;
    
    xx = (*pp = skip_whitespace (*pp));
    
    if (!(path = get_token (pp)) || !*path) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, *pp, "missing key path");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    *pp = skip_whitespace (*pp);
    
    if (!(name = get_token (pp)) || !*name) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, *pp, "missing key name");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    caret = (*pp = skip_whitespace (*pp));
    
    if ((ret = parse_binary_hex (pp, &data, &data_size))) {
    
        if (ret < 0) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "invalid hex digit '%c' in binary", **pp);
            
            ignore_rest_of_line (pp);
            return;
        
        }
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "odd number '%x' of hex digits", ret);
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    add_binary (root, start, path, name, xx, data, data_size);

}

static void handler_dword (char *start, char **pp, rf_key_t *root) {

    char *name, *path, *caret, *xx, *tmp;
    uint32_t val;
    
    xx = (*pp = skip_whitespace (*pp));
    
    if (!(path = get_token (pp)) || !*path) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, *pp, "missing key path");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    *pp = skip_whitespace (*pp);
    
    if (!(name = get_token (pp)) || !*name) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, *pp, "missing key name");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    caret = (*pp = skip_whitespace (*pp));
    
    if (!(tmp = get_token (pp)) || !*tmp) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "???");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    if (!parse_u32_strict (tmp, &val)) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "invalid dword '%s'", tmp);
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    add_dword (root, start, path, name, xx, val);

}

static void handler_expand (char *start, char **pp, rf_key_t *root) {

    char *name, *path;
    *pp = skip_whitespace (*pp);
    
    if (!(path = get_token (pp)) || !*path) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, *pp, "missing key path");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    *pp = skip_whitespace (*pp);
    
    if (!(name = get_token (pp)) || !*name) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, *pp, "missing key name");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    add_text_value (root, start, path, name, pp, REG_EXPAND_SZ);

}

static void handler_key (char *start, char **pp, rf_key_t *root) {

    *pp = skip_whitespace (*pp);
    
    if (**pp == '\n' || **pp == '\0') {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, *pp, "missing key path");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    add_key_path (root, pp);

}

static void handler_qword (char *start, char **pp, rf_key_t *root) {

    char *name, *path, *caret, *xx, *tmp;
    uint64_t val;
    
    xx = (*pp = skip_whitespace (*pp));
    
    if (!(path = get_token (pp)) || !*path) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, *pp, "missing key path");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    *pp = skip_whitespace (*pp);
    
    if (!(name = get_token (pp)) || !*name) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, *pp, "missing key name");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    caret = (*pp = skip_whitespace (*pp));
    
    if (!(tmp = get_token (pp)) || !*tmp) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "???");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    if (!parse_u64_strict (tmp, &val)) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "invalid qword '%s'", tmp);
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    add_qword (root, start, path, name, xx, val);

}

static void handler_sz (char *start, char **pp, rf_key_t *root) {

    char *name, *path;
    *pp = skip_whitespace (*pp);
    
    if (!(path = get_token (pp)) || !*path) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, *pp, "missing key path");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    *pp = skip_whitespace (*pp);
    
    if (!(name = get_token (pp)) || !*name) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, *pp, "missing key name");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    add_text_value (root, start, path, name, pp, REG_SZ);

}

static struct command_entry command_table[] = {

    {   "binary",       &handler_binary         },
    {   "dword",        &handler_dword          },
    {   "expand",       &handler_expand         },
    {   "key",          &handler_key            },
    {   "qword",        &handler_qword          },
    {   "sz",           &handler_sz             },
    
    {   0,              0                       }

};

static struct hashtab hashtab_commands = { 0 };

static struct command_entry *find_comand (char *name) {

    struct command_entry *entry;
    struct hashtab_name *key;
    
    if ((key = hashtab_get_key (&hashtab_commands, name))) {
    
        if ((entry = hashtab_get (&hashtab_commands, key))) {
            return entry;
        }
    
    }
    
    return 0;

}

int parse_input_file (const char *ifile, rf_key_t *root) {

    char *start, *caret, *token;
    FILE *fp;
    
    char *line, *line_end, *real_line;
    unsigned long real_line_len;
    
    unsigned long new_line_number = 1;
    unsigned long newlines;
    
    struct command_entry *poe;
    void *load_line_internal_data = NULL;
    
    if (!ifile || strcmp (ifile, "-") == 0) {
    
        set_filename_and_line_number (xstrdup ("<stdin>"), 0);
        fp = stdin;
    
    } else {
    
        set_filename_and_line_number (xstrdup (ifile), 0);
        
        if (!(fp = fopen (ifile, "r"))) {
        
            report_at (program_name, 0, REPORT_FATAL_ERROR, "Failed to open '%s' for reading", ifile);
            return 0;
        
        }
    
    }
    
    load_line_internal_data = load_line_create_internal_data (&new_line_number);
    
    while (!load_line (&line, &line_end, &real_line, &real_line_len, &newlines, fp, &load_line_internal_data)) {
    
        set_line_number (new_line_number);
        new_line_number += newlines + 1;
        
        start = line;
        
        while (line < line_end) {
        
            caret = skip_whitespace (line);
            
            if ((token = get_token (&line))) {
            
                if ((poe = find_comand (token))) {
                
                    poe->handler (start, &line, root);
                    
                    free (token);
                    continue;
                
                }
                
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "invalid command '%s'", token);
                
                ignore_rest_of_line (&line);
                free (token);
                
                continue;
            
            }
        
        }
    
    }
    
    load_line_destroy_internal_data (load_line_internal_data);
    
    if (fp != stdin) { fclose (fp); }
    return get_error_count () == 0;

}

int init_parser (void) {

    struct command_entry *table = command_table;
    struct hashtab *hashtab_table = &hashtab_commands;
    
    struct command_entry *entry;
    struct hashtab_name *key;
    
    for (entry = table; entry->name; entry++) {
    
        if (hashtab_get_key (hashtab_table, entry->name)) {
        
            report_at (program_name, 0, REPORT_ERROR, "duplicate entry '%s'", entry->name);
            continue;
        
        }
        
        if (!(key = hashtab_alloc_name (entry->name))) {
        
            report_at (program_name, 0, REPORT_ERROR, "failed to allocate memory for '%s'", entry->name);
            continue;
        
        }
        
        hashtab_put (hashtab_table, key, entry);
    
    }
    
    return get_error_count () > 0;

}

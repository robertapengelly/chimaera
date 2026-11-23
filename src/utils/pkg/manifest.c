/******************************************************************************
 * @file            tree.c
 *****************************************************************************/
#include    <ctype.h>
#include    <errno.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "pkg.h"
#include    "ll.h"
#include    "lib.h"

#include    "hashtab.h"
#include    "report.h"

#include    "manifest.h"

static int add_file (struct pkg_manifest *m, const char *source, const char *dest) {

    struct pkg_file *f;
    uint64_t new_capacity;
    
    if (m->file_count == m->file_capacity) {
    
        new_capacity = m->file_capacity ? m->file_capacity * 2 : 8;
        
        m->files = xrealloc (m->files, new_capacity * sizeof (*m->files));
        m->file_capacity = new_capacity;
    
    }
    
    f = &m->files[m->file_count];
    memset (f, 0, sizeof (*f));
    
    f->source = xstrdup (source);
    f->dest = xstrdup (dest);
    
    if (!f->source || !f->dest) {
    
        if (f->source) {
            free (f->source);
        }
        
        if (f->dest) {
            free (f->dest);
        }
        
        memset (f, 0, sizeof (*f));
        return -1;
    
    }
    
    m->file_count++;
    return 0;

}

static void handler_arch (char *start, char **pp) {

    char *token;
    char *caret;
    
    caret = (*pp = skip_whitespace (*pp));
    
    if (**pp != '=') {
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "expected key = value");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    caret = (*pp = skip_whitespace (*pp + 1));
    
    if (!(token = get_token (pp)) || !*token) {
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "failed to get arch");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    state->manifest.arch = token;

}

static void handler_desc (char *start, char **pp) {

    char *token;
    char *caret;
    
    caret = (*pp = skip_whitespace (*pp));
    
    if (**pp != '=') {
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "expected key = value");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    caret = (*pp = skip_whitespace (*pp + 1));
    
    while (**pp != '\n' && **pp != '\0') {
        (*pp)++;
    }
    
    token = xstrndup (caret, *pp - caret);
    
    if (!*token) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "failed to get description");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    state->manifest.desc = token;

}

static void handler_file (char *start, char **pp) {

    char *source, *dest;
    char *caret;
    
    caret = (*pp = skip_whitespace (*pp));
    
    if (!(source = get_token (pp)) || !*source) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "bad file entry");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    caret = (*pp = skip_whitespace (*pp));
    
    if (!(dest = get_token (pp)) || !*dest) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "bad file entry");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    if (add_file (&state->manifest, source, dest)) {
        report_at (get_filename (), get_line_number (), REPORT_ERROR, "failed to add file to package");
    }

}

static void handler_name (char *start, char **pp) {

    char *token;
    char *caret;
    
    caret = (*pp = skip_whitespace (*pp));
    
    if (**pp != '=') {
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "expected key = value");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    caret = (*pp = skip_whitespace (*pp + 1));
    
    if (!(token = get_token (pp)) || !*token) {
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "failed to get name");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    state->manifest.name = token;

}

static void handler_version (char *start, char **pp) {

    char *token;
    char *caret;
    
    caret = (*pp = skip_whitespace (*pp));
    
    if (**pp != '=') {
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "expected key = value");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    caret = (*pp = skip_whitespace (*pp + 1));
    
    if (!(token = get_token (pp)) || !*token) {
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "failed to get version");
        
        ignore_rest_of_line (pp);
        return;
    
    }
    
    state->manifest.version = token;

}

struct key_entry {

    const char *name;
    void (*handler) (char *start, char **pp);

};

static struct key_entry key_table[] = {

    {   "arch",         &handler_arch           },
    {   "file",         &handler_file           },
    {   "name",         &handler_name           },
    
    {   "version",      &handler_version        },
    {   "description",  &handler_desc           },
    
    {   0,              0                       }

};

static struct hashtab hashtab_keys = { 0 };

static struct key_entry *find_key (char *name) {

    struct key_entry *entry;
    struct hashtab_name *key;
    
    if ((key = hashtab_get_key (&hashtab_keys, name))) {
    
        if ((entry = hashtab_get (&hashtab_keys, key))) {
            return entry;
        }
    
    }
    
    return 0;

}

int parse_input_file (const char *ifile) {

    char *start, *caret, *token;
    FILE *fp;
    
    char *line, *line_end, *real_line;
    unsigned long real_line_len;
    
    unsigned long new_line_number = 1;
    unsigned long newlines;
    
    struct key_entry *poe;
    void *load_line_internal_data = NULL;
    
    if (!ifile || strcmp (ifile, "-") == 0) {
    
        set_filename_and_line_number (xstrdup ("<stdin>"), 0);
        fp = stdin;
    
    } else {
    
        set_filename_and_line_number (xstrdup (ifile), 0);
        
        if (!(fp = fopen (ifile, "r"))) {
        
            report_at (program_name, 0, REPORT_FATAL_ERROR, "failed to open '%s' for reading", ifile);
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
            
                if ((poe = find_key (token))) {
                
                    poe->handler (start, &line);
                    
                    free (token);
                    continue;
                
                }
                
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "unknown key '%s'", token);
                
                ignore_rest_of_line (&line);
                free (token);
                
                continue;
            
            }
        
        }
    
    }
    
    load_line_destroy_internal_data (load_line_internal_data);
    
    if (fp != stdin) { fclose (fp); }
    return get_error_count () > 0;

}

int init_parser (void) {

    struct hashtab *hashtab_table = &hashtab_keys;
    struct key_entry *table = key_table;
    
    struct key_entry *entry;
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
    
    memset (&state->manifest, 0, sizeof (state->manifest));
    return get_error_count () > 0;

}

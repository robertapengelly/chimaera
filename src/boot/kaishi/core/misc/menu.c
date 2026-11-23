/******************************************************************************
 * @file            menu.c
 *****************************************************************************/
#include    <kaishi/vfs/node.h>
#include    <kaishi/vfs/vfs.h>
#include    <kaishi/hashtab.h>
#include    <kaishi/console.h>
#include    <kaishi/ll.h>
#include    <kaishi/menu.h>
#include    <kaishi/misc.h>
#include    <kaishi/stdio.h>
#include    <kaishi/stdlib.h>
#include    <kaishi/string.h>
#include    <kaishi/vector.h>

#include    <ctype.h>
#include    <stddef.h>
#include    <stdint.h>
#include    <string.h>

static uint32_t menu_default = 0;
static uint64_t menu_timeout = 0;

static struct menu_entry *current_entry = 0;
static struct vector vec_entries = { 0 };

static int errors = 0;

struct keyword_entry {

    const char *name;
    void (*handler) (char *start, char **pp);

};

static const char *filename = 0;
static uint64_t line_number = 0;

static char *skip_whitespace (char *__p) {

    while (*__p == ' ' || *__p == '\t') {
        __p++;
    }
    
    return __p;

}

static char *symname (char **pp) {

    char *p = *pp;
    
    if (isalpha ((int) **pp)) {
    
        while (isalnum ((int) **pp)) {
            (*pp)++;
        }
        
        return kstrndup (p, *pp - p);
    
    }
    
    return 0;

}

uint8_t *ascii_to_string (char **pp) {

    uint8_t *p;
    char *tmp;
    
    size_t len = 0, i, j;
    int ch = 0;
    
    if (**pp == '"' || **pp == '\'') {
    
        ch = **pp;
        (*pp)++;
    
    }
    
    tmp = *pp;
    
    for (;;) {
    
        if (**pp == '\n' || **pp == '\0') {
            break;
        }
        
        len++;
        
        if (**pp == '\\') {
        
            if ((ch && *(*pp + 1) == ch) || *(*pp + 1) == '\\') {
            
                *pp += 2;
                continue;
            
            }
        
        } else if (ch && **pp == ch) {
            break;
        }
        
        (*pp)++;
    
    }
    
    p = kmalloc (len + 1);
    len = (*pp - tmp);
    
    for (i = 0, j = 0; i < len; i++) {
    
        if (tmp[i] == '\\') {
        
            if ((ch && (int) tmp[i + 1] == ch) || tmp[i + 1] == '\\') {
                continue;
            }
        
        }
        
        p[j++] = tmp[i];
    
    }
    
    p[j] = 0;
    
    if (ch) { (*pp)++; }
    return p;

}

static uint32_t genhash (const char *arg) {

    size_t len = strlen (arg), i;
    uint32_t hash = 0;
    
    for (i = 0; i < len; i++) { hash = (((hash << 5) | (hash >> 27)) ^ arg[i]) & 0xFFFFFFFF; }
    return hash;

}

static uint64_t ascii_to_decimal (char **pp) {

    uint64_t res = 0;
    
    if (**pp == '0') {
    
        if (tolower ((int) *(*pp + 1)) == 'x') {
        
            *pp += 2;
            
            if (!isxdigit ((int) **pp)) {
            
                kprintf ("%s:%llu: error: invalid hex value\n", filename, line_number);
                
                errors++;
                return 0;
            
            }
            
            while (isxdigit ((int) **pp)) {
            
                if (isalpha ((int) **pp)) {
                    res = (res * 16) + ((toupper ((int) **pp) - 'A') + 10);
                } else {
                    res = (res * 16) + (**pp - '0');
                }
                
                (*pp)++;
            
            }
            
            return res;
        
        }
        
        while (isdigit ((int) **pp)) {
        
            if (**pp > '7') {
            
                kprintf ("%s:%llu: error: invalid digit in octal\n", filename, line_number);
                
                errors++;
                return 0;
            
            }
            
            res = (res * 8) + (**pp - '0');
            (*pp)++;
        
        }
        
        return res;
    
    }
    
    while (isdigit ((int) **pp)) {
    
        res = (res * 10) + (**pp - '0');
        (*pp)++;
    
    }
    
    return res;

}

static struct hashtab hashtab_keywords = { 0 };

static void handler_default (char *start, char **pp) {

    const char *arg;
    uint32_t hash;
    
    *pp = skip_whitespace (*pp);
    (void) start;
    
    if (!(arg = symname (pp))) {
    
        kprintf ("%s:%llu: error: default has no name\n", filename, line_number);
        
        errors++;
        return;
    
    }
    
    hash = genhash (arg);
    kfree (arg);
    
    menu_default = hash;

}

static void handler_kernel (char *start, char **pp) {

    *pp = skip_whitespace (*pp);
    (void) start;
    
    if (!current_entry) {
    
        kprintf ("%s:%llu: error: kernel without menuentry\n", filename, line_number);
        
        while (**pp != '\0' && **pp != '\n') {
            (*pp)++;
        }
        
        errors++;
        return;
    
    }
    
    if (**pp == '\0' || **pp == '\n') {
    
        kprintf ("%s:%lu: error: kernel without value\n", filename, line_number);
        
        errors++;
        return;
    
    }
    
    if (current_entry->kernel) {
    
        kprintf ("%s:%llu: warning: more than one kernel specified\n", filename, line_number);
        kfree (current_entry->title);
    
    }
    
    current_entry->kernel = ascii_to_string (pp);

}

static void handler_menuentry (char *start, char **pp) {

    const char *arg;
    int i;
    
    struct menu_entry *me;
    uint32_t hash;
    
    *pp = skip_whitespace (*pp);
    (void) start;
    
    if (!(arg = symname (pp))) {
    
        kprintf ("%s:%llu: error: menuentry has no name\n", filename, line_number);
        
        errors++;
        return;
    
    }
    
    hash = genhash (arg);
    
    for (i = 0; i < vec_entries.length; i++) {
    
        if (!(me = vec_entries.data[i])) {
            continue;
        }
        
        if (me->hash == hash) {
        
            kprintf ("%s:%llu: error: entry hash collision\n", filename, line_number);
            
            current_entry = me;
            errors++;
            
            kfree (arg);
            return;
        
        }
    
    }
    
    current_entry = kmalloc (sizeof (*current_entry));
    vec_push (&vec_entries, current_entry);
    
    current_entry->name = arg;
    current_entry->hash = hash;

}

static void handler_module (char *start, char **pp) {

    *pp = skip_whitespace (*pp);
    (void) start;
    
    if (!current_entry) {
    
        kprintf ("%s:%llu: error: module without menuentry\n", filename, line_number);
        
        while (**pp != '\0' && **pp != '\n') {
            (*pp)++;
        }
        
        errors++;
        return;
    
    }
    
    if (**pp == '\0' || **pp == '\n') {
    
        kprintf ("%s:%lu: error: module without value\n", filename, line_number);
        
        errors++;
        return;
    
    }
    
    vec_push (&current_entry->vec_modules, ascii_to_string (pp));

}

static void handler_timeout (char *start, char **pp) {

    *pp = skip_whitespace (*pp);
    (void) start;
    
    menu_timeout = ascii_to_decimal (pp);

}

static void handler_title (char *start, char **pp) {

    *pp = skip_whitespace (*pp);
    (void) start;
    
    if (!current_entry) {
    
        kprintf ("%s:%llu: error: title without menuentry\n", filename, line_number);
        
        while (**pp != '\0' && **pp != '\n') {
            (*pp)++;
        }
        
        errors++;
        return;
    
    }
    
    if (current_entry->title) {
    
        kprintf ("%s:%llu: warning: more than one title specified\n", filename, line_number);
        kfree (current_entry->title);
    
    }
    
    if (current_entry) { current_entry->title = ascii_to_string (pp); }

}

static struct keyword_entry keywords_table[] = {

    {   "default",      &handler_default        },
    {   "kernel",       &handler_kernel         },
    {   "menuentry",    &handler_menuentry      },
    {   "module",       &handler_module         },
    {   "timeout",      &handler_timeout        },
    {   "title",        &handler_title          },
    
    {   0,              0                       }

};

static struct keyword_entry *find_keyword (const char *name) {

    struct hashtab_name *key;
    struct keyword_entry *entry;
    
    char *lower = kstrdup (name);
    size_t len = strlen (name), i;
    
    for (i = 0; i < len; i++) {
        lower[i] = tolower ((int) lower[i]);
    }
    
    if ((key = hashtab_get_key (&hashtab_keywords, lower))) {
    
        if ((entry = hashtab_get (&hashtab_keywords, key))) {
        
            kfree (lower);
            return entry;
        
        }
    
    }
    
    kfree (lower);
    return 0;

}

extern int sleep (size_t seconds);

static int run_timeout (void) {

    int len = 0, key = 0;
    
    for (;;) {
    
        if (!menu_timeout) {
            return key;
        }
        
        len = kprintf (" Automatically booting in %llu %s\r", menu_timeout, menu_timeout == 1 ? "second" : "seconds");
        
        if ((key = sleep (1))) {
            menu_timeout = 1;
        }
        
        menu_timeout--;
        
        while (len--) {
            kprintf (" ");
        }
        
        kprintf ("\r");
    
    }

}

size_t get_screen_height (void);
size_t get_screen_width (void);

struct menu_entry *run_menu (void) {

    struct menu_entry *me;
    
    int index = 0, key = 0, count, skip, i;
    int x = -1, y = -1;
    
    int log_level = get_log_level (), screen_height = get_screen_height (), screen_width = get_screen_width ();
    int previous_skip = -1, menu_end = (screen_height / 2) - 2;
    
    if (vec_entries.length == 0) {
        return 0;
    }
    
    set_log_level (2);
    
    if (menu_default) {
    
        for (i = 0; i < vec_entries.length; i++) {
        
            if (!(me = vec_entries.data[i])) {
                continue;
            }
            
            if (me->hash == menu_default) {
            
                index = i;
                break;
            
            }
        
        }
    
    }
    
    console_hide_cursor ();
    console_clear ();
    
    if (menu_timeout > 0) {
    
        for (;;) {
        
            if (key == 0x0D) {
                break;
            }
            
            if (x >= 0) {
            
                console_clear_line (x, y);
                kprintf ("    %.*s\n", screen_width - 8, ((struct menu_entry *) vec_entries.data[index])->title);
            
            }
            
            if (key == 0x01) {
            
                if (index > 0) {
                
                    index--;
                    y--;
                
                }
            
            } else if (key == 0x02) {
            
                if (index < vec_entries.length - 1) {
                
                    index++;
                    y++;
                
                }
            
            }
            
            skip = 0;
            key = 0;
            
            if (index >= menu_end) { skip = index - (menu_end - 1); }
            count = vec_entries.length - skip;
            
            if (count > menu_end) {
                count = menu_end;
            }
            
            if (skip != previous_skip) {
            
                int padding = (screen_width / 2) - 8;
                
                previous_skip = skip;
                console_clear ();
                
                console_highlight (0, 0, 0);
                
                for (i = 0; i < padding; i++) {
                    kprintf (" ");
                }
                
                kprintf ("Kaishi Boot Menu\n\n");
                console_reset_colors ();
                
                for (i = skip; i < skip + count; i++) {
                
                    if (!(me = vec_entries.data[i])) {
                        continue;
                    }
                    
                    if (i == index) {
                    
                        console_get_cursor (&x, &y);
                        console_highlight (x, y, 2);
                        
                        kprintf ("  %.*s\n", screen_width - 8, me->title);
                        console_reset_colors ();
                        
                        continue;
                    
                    }
                    
                    kprintf ("    %.*s\n", screen_width - 8, me->title);
                
                }
            
            } else {
            
                console_highlight (x, y, 2);
                
                kprintf ("  %.*s\n", screen_width - 8, ((struct menu_entry *) vec_entries.data[index])->title);
                console_reset_colors ();
            
            }
            
            if (menu_timeout) {
            
                kprintf ("\n");
                
                if (!(key = run_timeout ()) || key == 0x0D) {
                    break;
                }
                
                continue;
            
            }
            
            key = kbd_wait_key ();
        
        }
    
    }
    
    set_log_level (log_level);
    
    console_clear ();
    console_show_cursor ();
    
    return vec_entries.data[index];

}

int init_menu (void) {

    struct keyword_entry *entry;
    struct hashtab_name *key;
    
    for (entry = keywords_table; entry->name; entry++) {
    
        if (hashtab_get_key (&hashtab_keywords, entry->name)) {
        
            kprintf ("error: duplicate entry '%s'", entry->name);
            continue;
        
        }
        
        if (!(key = hashtab_alloc_name (entry->name))) {
        
            kprintf ("error: failed to allocate memory for '%s'", entry->name);
            continue;
        
        }
        
        hashtab_put (&hashtab_keywords, key, entry);
    
    }
    
    return errors;

}

int parse_menu (const char *path) {

    struct fs_node *node;
    char *line, *line_end;
    
    uint64_t new_line_number = 1;
    uint64_t newlines;
    
    int errors = 0;
    void *load_line_internal_data;
    
    struct keyword_entry *ke;
    const char *start, *arg;
    
    filename = path;
    
    if (!(node = vfs_open (path))) {
    
        kprintf ("error: failed to open '%s' for reading\n", path);
        return 0;
    
    }
    
    load_line_internal_data = load_line_internal_data = load_line_create_internal_data (&new_line_number);
    
    while (!load_line (&line, &line_end, &newlines, node, &load_line_internal_data)) {
    
        start = line;
        
        line_number = new_line_number;
        new_line_number += newlines + 1;
        
        line = skip_whitespace (line);
        
        while (line < line_end) {
        
            arg = symname (&line);
            
            if ((ke = find_keyword (arg))) {
                ke->handler (start, &line);
            } else {
            
                kprintf ("%s:%llu: error: unknown keyword '%s'\n", filename, line_number, arg);
                errors++;
            
            }
            
            line = skip_whitespace (line);
            
            if (*line != '\0' && *line != '\n') {
            
                kprintf ("%s:%llu: error: junk '%c' at the end of line\n", filename, line_number, *line);
                
                while (*line != '\0' && *line != '\n') {
                    line++;
                }
                
                errors++;
            
            }
        
        }
    
    }
    
    load_line_destroy_internal_data (load_line_internal_data);
    
    kclose (node);
    return errors;

}

/******************************************************************************
 * @file            tree.c
 *****************************************************************************/
#include    <stdint.h>
#include    <stdlib.h>
#include    <string.h>

#include    "regf.h"
#include    "lib.h"
#include    "tree.h"
#include    "report.h"

const char *canonical_value_name (const char *name) {

    if (name && strcmp (name, "@") == 0) {
        return "";
    }
    
    return name;

}

value_t *find_value (value_t *head, const char *name) {

    value_t *v;
    
    for (v = head; v; v = v->next) {
    
        if (strcmp (v->name, name) == 0) {
            return v;
        }
    
    }
    
    return 0;

}

static rf_key_t *find_child (rf_key_t *parent, const char *name) {

    rf_key_t *k;
    
    for (k = parent->children; k; k = k->next) {
    
        if (strcmp (k->name, name) == 0) {
            return k;
        }
    
    }
    
    return 0;

}

rf_key_t *append_child (rf_key_t *parent, const char *name) {

    rf_key_t *k = find_child (parent, name);
    rf_key_t *tail;
    
    if (k) { return k; }
    k = key_new (name, parent);
    
    if (!parent->children) {
        parent->children = k;
    } else {
    
        tail = parent->children;
        
        while (tail->next) {
            tail = tail->next;
        }
        
        tail->next = k;
    
    }
    
    return k;

}

void add_value_node (rf_key_t *k, value_t *v) {

    value_t *tail;
    
    if (!k->values) {
        k->values = v;
    } else {
    
        tail = k->values;
        
        while (tail->next) {
            tail = tail->next;
        }
        
        tail->next = v;
    
    }

}

value_t *value_new (const char *name, uint32_t type, const void *data, uint32_t data_len) {

    value_t *v = xmalloc (sizeof (*v));
    
    v->name = xstrdup (name);
    v->type = type;
    
    v->data = xmalloc (data_len ? data_len : 1);
    v->data_len = data_len;
    
    if (data_len) {
        memcpy (v->data, data, data_len);
    }
    
    v->data_off = INVALID_OFFSET;
    v->vk_off = INVALID_OFFSET;
    
    return v;

}

rf_key_t *collapse_synthetic_root (rf_key_t *root) {

    rf_key_t *child, *new_root, *c;
    
    if (!root) {
        return root;
    }
    
    /* If synthetic root has values, keep it */
    if (root->values) {
        return root;
    }
    
    child = root->children;
    
    /* No children: keep as-is */
    if (!child) {
        return root;
    }
    
    /* One child: old behavior, collapse to that child */
    if (!child->next) {
    
        root->children = 0;
        child->parent = 0;
        
        free (root->name);
        free (root);
        
        return child;
    
    }
    
    /**
     * Multiple top-level children:
     *
     *     keep them, but do not leave the hive root unnamed.
     *     Wrap them in a real root key so Windows will accept the hive.
     */
    new_root = key_new ("ROOT", 0);
    new_root->children = root->children;
    
    root->children = 0;
    
    for (c = new_root->children; c; c = c->next) {
        c->parent = new_root;
    }
    
    free (root->name);
    free (root);
    
    return new_root;

}

rf_key_t *add_key_path (rf_key_t *root, char **pp) {

    char *q;
    int had_sep;
    
    rf_key_t *cur = root;
    
    while (**pp != '\n' && **pp != '\0') {
    
        while (**pp == '\\' || **pp == '.') {
            (*pp)++;
        }
        
        if (**pp == '\n' || **pp == '\0') {
            break;
        }
        
        q = *pp;
        
        while (*q != '\n' && *q != '\0') {
        
            if (*q == '\\' || *q == '.') {
                break;
            }
            
            q++;
        
        }
        
        had_sep = (*q == '\\' || *q == '.');
        
        if (had_sep || *q == '\n') {
            *q = '\0';
        }
        
        cur = append_child (cur, *pp);
        
        if (!had_sep) {
        
            *pp = q;
            break;
        
        }
        
        *pp = skip_whitespace (q + 1);
    
    }
    
    return cur;

}

rf_key_t *key_new (const char *name, rf_key_t *parent) {

    rf_key_t *k = xmalloc (sizeof (*k));
    
    k->name = xstrdup (name);
    k->parent = parent;
    
    k->subkey_list_off = INVALID_OFFSET;
    k->value_list_off = INVALID_OFFSET;
    k->nk_off = INVALID_OFFSET;
    
    return k;

}

void add_binary (rf_key_t *root, const char *start, const char *path, const char *name, const char *caret, const void *data, uint32_t data_size) {
 
    char *tmp, *p;
    rf_key_t *k;
    
    name = canonical_value_name (name);
    tmp = (p = xstrdup (path));
    
    k = add_key_path (root, &p);
    free (tmp);
    
    if (find_value (k->values, name)) {
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "duplicate value '%s' in key '%s'", name, path);
        return;
        
    }
    
    add_value_node (k, value_new (name, REG_BINARY, data, data_size));

}

void add_dword (rf_key_t *root, const char *start, const char *path, const char *name, const char *caret, uint32_t dword) {

    char *tmp, *p;
    rf_key_t *k;
    
    unsigned char b[4];
    wr32 (b, dword);
    
    name = canonical_value_name (name);
    tmp = (p = xstrdup (path));
    
    k = add_key_path (root, &p);
    free (tmp);
    
    if (find_value (k->values, name)) {
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "duplicate value '%s' in key '%s'", name, path);
        return;
        
    }
    
    add_value_node (k, value_new (name, REG_DWORD, b, 4));

}

void add_qword (rf_key_t *root, const char *start, const char *path, const char *name, const char *caret, uint64_t qword) {

    char *tmp, *p;
    rf_key_t *k;
    
    unsigned char b[8];
    wr64 (b, qword);
    
    name = canonical_value_name (name);
    tmp = (p = xstrdup (path));
    
    k = add_key_path (root, &p);
    free (tmp);
    
    if (find_value (k->values, name)) {
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, caret, "duplicate value '%s' in key '%s'", name, path);
        return;
        
    }
    
    add_value_node (k, value_new (name, REG_QWORD, b, 8));

}

void add_text_value (rf_key_t *root, const char *start, const char *path, const char *name, char **pp, uint32_t type) {

    char *tmp, *p;
    rf_key_t *k;
    
    uint32_t len;
    uint8_t *u16;
    
    name = canonical_value_name (name);
    tmp = (p = xstrdup (path));
    
    k = add_key_path (root, &p);
    free (tmp);
    
    if (find_value (k->values, name)) {
        
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, start, *pp - strlen (name), "duplicate value '%s' in key '%s'", name, path);
        
        ignore_rest_of_line (pp);
        return;
        
    }
    
    *pp = skip_whitespace (*pp);
    u16 = ascii_to_utf16le (pp, &len);
    
    add_value_node (k, value_new (name, type, u16, len));
    free (u16);

}

void stats_collect (rf_key_t *k, uint32_t depth, stats_t *s) {

    value_t *v;
    rf_key_t *c;
    
    if (!k || !s) {
        return;
    }
    
    s->key_count++;
    
    if (depth > s->max_depth) {
        s->max_depth = depth;
    }
    
    for (v = k->values; v; v = v->next) {
        s->value_count++;
    }
    
    for (c = k->children; c; c = c->next) {
        stats_collect (c, depth + 1U, s);
    }

}

uint32_t count_values (rf_key_t *k) {

    uint32_t n = 0;
    value_t *v;
    
    for (v = k->values; v; v = v->next) {
        n++;
    }
    
    return n;
}

uint32_t count_children (rf_key_t *k) {

    uint32_t n = 0;
    rf_key_t *c;
    
    for (c = k->children; c; c = c->next) {
        n++;
    }
    
    return n;

}

void sort_children (rf_key_t *parent) {

    rf_key_t *sorted = 0, *cur = parent->children;
    rf_key_t *next, *p;
    
    while (cur) {
    
        next = cur->next;
        
        if (!sorted || ascii_casecmp (cur->name, sorted->name) < 0) {
        
            cur->next = sorted;
            sorted = cur;
        
        } else {
        
            p = sorted;
            
            while (p->next && ascii_casecmp (cur->name, p->next->name) >= 0) {
                p = p->next;
            }
            
            cur->next = p->next;
            p->next = cur;
        
        }
        
        cur = next;
    
    }
    
    parent->children = sorted;

}

uint32_t max_subkey_name_u16 (rf_key_t *k) {

    uint32_t m = 0;
    rf_key_t *c;
    
    for (c = k->children; c; c = c->next) {
    
        uint32_t x = utf16le_len_bytes_from_ascii (c->name);
        
        if (x > m) {
            m = x;
        }
    
    }
    
    return m;

}

uint32_t max_value_data_size (rf_key_t *k) {

    uint32_t m = 0;
    value_t *v;
    
    for (v = k->values; v; v = v->next) {
    
        if (v->data_len > m) {
            m = v->data_len;
        }
    
    }
    
    return m;

}

uint32_t max_value_name_u16 (rf_key_t *k) {

    uint32_t m = 0;
    value_t *v;
    
    for (v = k->values; v; v = v->next) {
    
        uint32_t x = utf16le_len_bytes_from_ascii (v->name);
        
        if (x > m) {
            m = x;
        }
    
    }
    
    return m;

}

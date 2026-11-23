/******************************************************************************
 * @file            vfs.c
 *****************************************************************************/
#include    <kaishi/vfs/vfs.h>
#include    <kaishi/spin.h>
#include    <kaishi/stdio.h>
#include    <kaishi/stdlib.h>
#include    <kaishi/string.h>
#include    <kaishi/tree.h>

#include    <dirent.h>
#include    <stdio.h>
#include    <string.h>

static struct fs_node *fs_root = 0;
static tree_t *fs_tree = 0;

static spin_lock_t tmp_refcount_lock = { 0 };
static spin_lock_t tmp_read_lock = { 0 };

static void debug_print_vfs_tree_node (struct tree_node *node, size_t height) {

    struct vfs_entry *fnode;
    
    struct list_node *child;
    size_t i;
    
    if (!node || !node->value) {
        return;
    }
    
    for (i = 0; i < height; i++) {
        kprintf ("\t");
    }
    
    fnode = (struct vfs_entry *) node->value;
    
    if (fnode->file) {
        kprintf ("%s -> %P\n", fnode->name, fnode->file);
    } else {
        kprintf ("%s -> (empty)\n", fnode->name);
    }
    
    if (node->children) {
    
        for (child = node->children->head; child; child = child->next) {
            debug_print_vfs_tree_node (child->value, height + 1);
        }
    
    }

}

static char *canonicalize_path (const char *cwd, const char *path) {

    char *p = path, *q;
    size_t len = 0;
    
    if (!path) {
        return 0;
    }
    
    if (!(*p == '/' || *p == '\\') && cwd) {
    
        len += strlen (cwd);
        
        if (strncmp (p, cwd, strlen (cwd)) == 0) {
        
            path += strlen (cwd);
            p += strlen (cwd);
        
        }
    
    }
    
    while (*p != '\0') {
    
        len++;
        
        if (*p == '/' || *p == '\\') {
        
            while (*p == '/' || *p == '\\') {
                p++;
            }
            
            continue;
        
        }
        
        p++;
    
    }
    
    q = (p = kmalloc (len + 1));
    
    if (!(*path == '/' || path == '\\') && cwd) {
    
        q += sprintf (q, "%s", cwd);
        
        if (*q == '/' || *q == '\\') {
            *q++ = '/';
        }
    
    }
    
    while (*path != '\0') {
    
        if (*path == '/' || *path == '\\') {
        
            *q++ = '/';
            
            while (*path == '/' || *path == '\\') {
                path++;
            }
            
            continue;
        
        }
        
        *q++ = *path++;
    
    }
    
    return p;

}

static void close_fs (struct fs_node *node) {

    if (!node) {
        return;
    }
    
    spin_lock (&tmp_refcount_lock);
    
    if (node->refcount < 0) {
    
        spin_unlock (&tmp_refcount_lock);
        return;
    
    }
    
    node->refcount--;
    
    if (node->refcount == 0) {
    
        if (node->kclose) {
            node->kclose (node);
        }
    
    }
    
    spin_unlock (&tmp_refcount_lock);

}

static struct fs_node *get_mount_point (char *path, size_t path_depth, char **outpath, size_t *outdepth) {

    struct tree_node *node, *tchild;
    struct vfs_entry *ent;
    struct fs_node *last;
    
    char *at = *outpath;
    int found;
    
    struct list_node *child;
    size_t _depth = 1, _tree_depth = 0, depth;
    
    for (depth = 0; depth <= path_depth; ++depth) {
        path += strlen (path) + 1;
    }
    
    node = fs_tree->root;
    last = fs_root;
    
    while (1) {
    
        if (at >= path) {
            break;
        }
        
        found = 0;
        
        if (node->children) {
        
            for (child = node->children->head; child; child = child->next) {
            
                tchild = (struct tree_node *) child->value;
                ent = (struct vfs_entry *) tchild->value;
                
                if (!strcmp (ent->name, at)) {
                
                    at = at + strlen (at) + 1;
                    node = tchild;
                    
                    if (ent->file) {
                    
                        _tree_depth = _depth;
                        
                        last = ent->file;
                        *outpath = at;
                    
                    }
                    
                    found = 1;
                    break;
                
                }
            
            }
            
            if (!found) {
                break;
            }
            
            _depth++;
        
        }
    
    }
    
    *outdepth = _tree_depth;
    return last;

}

static struct fs_node *open_fs (struct fs_node *node, const char *path, size_t depth) {

    if (!node) { return node; }
    spin_lock (&tmp_refcount_lock);
    
    if (node->flags & VFS_DIRECTORY) {
    
        if (node->check_and_mount) {
        
            if (node->check_and_mount (node)) {
            
                spin_unlock (&tmp_refcount_lock);
                return 0;
            
            }
        
        }
        
        if (node->kopen) {
        
            if (!(node = node->kopen (node, path, depth))) {
            
                spin_unlock (&tmp_refcount_lock);
                return 0;
            
            }
            
            if (node->refcount >= 0) {
                node->refcount++;
            }
            
            spin_unlock (&tmp_refcount_lock);
            return node;
        
        }
        
        spin_unlock (&tmp_refcount_lock);
        return 0;
    
    }
    
    if (node->refcount >= 0) {
        node->refcount++;
    }
    
    spin_unlock (&tmp_refcount_lock);
    return node;

}

static char *path_tokenize (char *path, size_t len, size_t *depth) {

    char *path_offset = path;
    *depth = 0;
    
    while (path_offset < path + len) {
    
        if (*path_offset == '/' || *path_offset == '\\') {
        
            *path_offset = '\0';
            (*depth)++;
        
        }
        
        path_offset++;
    
    }
    
    /*if (len > 0) {
    
        if (path[len - 1] == '\0') {
            (*depth)--;
        }
    
    }*/
    
    path[len] = '\0';
    return path + 1;

}

#define     MIN(a, b)                   ((a) < (b) ? (a) : (b))

static int readdir_mapper (void *data, uint32_t index, struct dirent *de) {

    struct fs_node *node = (struct fs_node *) data;
    struct tree_node *d;
    
    unsigned long i = 0;
    size_t len;
    
    struct tree_node *tchild;
    struct vfs_entry *n;
    
    struct list_node *child;
    
    if (!(d = (struct tree_node *) node->device)) {
        return 0;
    }
    
    if (index == 0) {
    
        strcpy (de->d_name, ".");
        de->d_ino = 0;
        
        return 1;
    
    } else if (index == 1) {
    
        strcpy (de->d_name, "..");
        de->d_ino = 1;
        
        return 1;
    
    }
    
    index -= 2;
    
    if (d->children) {
    
        for (child = d->children->head; child; child = child->next) {
        
            if (i == index) {
            
                tchild = (struct tree_node *) child->value;
                len = strlen ((n = (struct vfs_entry *) tchild->value)->name) + 1;
                
                memcpy (de->d_name, n->name, MIN (256U, len));
                de->d_ino = i;
                
                return 1;
            
            }
            
            i++;
        
        }
    
    }
    
    return 0;

}

static struct fs_node *vfs_mapper (void) {

    struct fs_node *node = kmalloc (sizeof (struct fs_node));
    
    node->flags = VFS_DIRECTORY;
    node->readdir = readdir_mapper;
    
    return node;

}

void map_vfs_directory (const char *path) {

    struct fs_node *f;
    struct tree_node_t *e;
    
    e = vfs_mount ((char *) path, f = vfs_mapper ());
    
    if (!strcmp (path, "/")) {
        f->device = fs_tree->root;
    } else {
        f->device = e;
    }

}

void vfs_close (struct fs_node *node) {
    close_fs (node);
}

uint64_t vfs_get_disk_checksum (void) {

    struct tree_node *root_node;
    struct vfs_entry *root;
    
    root_node = fs_tree->root;
    
    root = (struct vfs_entry *) root_node->value;
    return root->file->checksum_hash;

}

void *vfs_mount (const char *path, struct fs_node *local_root) {

    struct tree_node *ret_val = 0, *root_node;
    char *p, *i;
    
    size_t path_len;
    
    if (!fs_tree) {
    
        panic ("VFS hasn't been initialized, you can't mount things yet.\n");
        return 0;
    
    }
    
    path_len = strlen (p = canonicalize_path ("/", path));
    
    if (!p || p[0] != '/') {
    
        panic ("Path must be absolute for mountpoints.\n");
        return 0;
    
    }
    
    i = p;
    
    while (i < p + path_len) {
    
        if (*i == '/') {
            *i = '\0';
        }
        
        i++;
    
    }
    
    p[path_len] = '\0';
    i = p + 1;
    
    root_node = fs_tree->root;
    
    if (*i == '\0') {
    
        struct vfs_entry *root = (struct vfs_entry *) root_node->value;
        
        if (root->file) {
            kprintf ("Path %s already mounted, unmount before trying to mount something else.\n", path);
        }
        
        root->file = local_root;
        
        fs_root = local_root;
        ret_val = root_node;
    
    } else {
    
        struct tree_node *node, *tchild;
        struct vfs_entry *ent;
        
        char *at;
        int found;
        
        struct list_node *child;
        
        node = root_node;
        at = i;
        
        for (;;) {
        
            found = 0;
            
            if (at >= p + path_len) {
                break;
            }
            
            for (child = node->children->head; child; child = child->next) {
            
                tchild = (struct tree_node *) child->value;
                ent = (struct vfs_entry *) tchild->value;
                
                if (!strcmp (ent->name, at)) {
                
                    found = 1;
                    
                    node = tchild;
                    ret_val = node;
                    
                    break;
                
                }
            
            }
            
            if (!found) {
            
                ent = kmalloc (sizeof (*ent));
                ent->name = kstrdup (at);
                
                node = tree_node_insert_child (fs_tree, node, ent);
            
            }
            
            at = at + strlen (at) + 1;
        
        }
        
        ent = (struct vfs_entry *) node->value;
        
        if (ent->file) {
            kprintf ("Path %s already mounted, unmount before trying to mount something else.\n", path);
        }
        
        ent->file = local_root;
        ret_val = node;
    
    }
    
    kfree (p);
    return ret_val;

}

struct fs_node *vfs_open (const char *path) {

    struct fs_node *node;
    
    char *path_offset, *temp_path;
    size_t path_depth = 0, depth = 0, path_len;
    
    if (!path) {
        return 0;
    }
    
    temp_path = canonicalize_path ("/", path);
    path_len = strlen (temp_path);
    
    if (path_len > 1) {
    
        path_offset = path_tokenize (temp_path, path_len, &path_depth);
        
        if (!(node = get_mount_point (temp_path, path_depth, &path_offset, &depth))) {
        
            kfree (temp_path);
            return 0;
        
        }
    
    } else {
        node = fs_root;
    }
    
    node = open_fs (node, path_offset, path_depth - depth);
    kfree (temp_path);
    
    return node;

}

int vfs_read (struct fs_node *node, void *buf, size_t count) {

    int ret = -1;
    spin_lock (&tmp_read_lock);
    
    if (node->kread) {
        ret = node->kread (node, buf, count);
    }
    
    spin_unlock (&tmp_read_lock);
    return ret;

}

void debug_print_vfs_tree (void) {
    debug_print_vfs_tree_node (fs_tree->root, 0);
}

void vfs_install (void) {

    struct vfs_entry *root;
    fs_tree = tree_create ();
    
    root = kmalloc (sizeof (*root));
    root->name = kstrdup ("[root]");
    
    tree_set_root (fs_tree, root);

}

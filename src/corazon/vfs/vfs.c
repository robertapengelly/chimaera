/******************************************************************************
 * @file            vfs.c
 *****************************************************************************/
#include    <corazon/tasking/process.h>
#include    <corazon/utils/misc.h>
#include    <corazon/utils/tree.h>
#include    <corazon/vfs/vfs.h>
#include    <corazon/console.h>
#include    <corazon/spin.h>

#include    <dirent.h>
#include    <errno.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <string.h>

extern int cmos_get_unix_time (uint64_t *seconds);
static int64_t vfs_last_creation_time = 0;

static struct fs_node *fs_root = 0;
static tree_t *fs_tree = 0;

static spin_lock_t tmp_refcount_lock = { 0 };
static spin_lock_t tmp_read_lock = { 0 };
static spin_lock_t tmp_seek_lock = { 0 };
static spin_lock_t tmp_write_lock = { 0 };

static void debug_print_vfs_tree_node (struct tree_node *node, size_t height) {

    struct vfs_entry *fnode;
    
    struct list_node *child;
    size_t i;
    
    if (!node || !node->value) {
        return;
    }
    
    for (i = 0; i < height; i++) {
    
        kprintf ("\t");
        dprintf ("\t");
    
    }
    
    fnode = (struct vfs_entry *) node->value;
    
    if (fnode->file) {
    
        kprintf ("%s -> %P\n", fnode->name, fnode->file);
        dprintf ("%s -> %P\n", fnode->name, fnode->file);
    
    } else {
    
        kprintf ("%s -> (empty)\n", fnode->name);
        dprintf ("%s -> (empty)\n", fnode->name);
    
    }
    
    if (node->children) {
    
        for (child = node->children->head; child; child = child->next) {
            debug_print_vfs_tree_node (child->value, height + 1);
        }
    
    }

}

static char *canonicalize_path (const char *cwd, const char *path, int *error) {

    const char *src, *part;
    char *p, *q;
    
    size_t len, part_len;
    int pass, relative;
    
    if (!path) {
    
        *error = -EFAULT;
        return 0;
    
    }
    
    relative = *path != '/' && *path != '\\';
    len = strlen (path) + (relative && cwd ? strlen (cwd) : 0) + 2;
    
    if (len > PROCESS_CWD_MAX) {
    
        *error = -ENAMETOOLONG;
        return 0;
    
    }
    
    if (!(p = kmalloc (len))) {
    
        *error = -ENOMEM;
        return 0;
    
    }
    
    q = p;
    *q++ = '/';
    
    for (pass = 0; pass < 2; pass++) {
    
        if (pass == 0) {
        
            if (!relative || !cwd) {
                continue;
            }
            
            src = cwd;
        
        } else {
            src = path;
        }
        
        while (*src) {
        
            while (*src == '/' || *src == '\\') {
                src++;
            }
            
            if (!*src) {
                break;
            }
            
            part = src;
            
            while (*src && *src != '/' && *src != '\\') {
                src++;
            }
            
            part_len = (size_t) (src - part);
            
            if (part_len == 1 && part[0] == '.') {
                continue;
            }
            
            if (part_len == 2 && part[0] == '.' && part[1] == '.') {
            
                while (q > p + 1 && q[-1] != '/') {
                    q--;
                }
                
                if (q > p + 1) {
                    q--;
                }
                
                continue;
            
            }
            
            if (q > p + 1) {
                *q++ = '/';
            }
            
            memcpy (q, part, part_len);
            q += part_len;
        
        }
    
    }
    
    *q = '\0';
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

static struct fs_node *open_fs (struct fs_node *node, const char *path, size_t depth, int flags, int *error) {

    int64_t creation_time;
    
    if (!node) { return node; }
    creation_time = node->creation_time;
    
    spin_lock (&tmp_refcount_lock);
    
    if ((node->flags & INO_BLK) || (node->flags & INO_CHR)) {
    
        if ((node->flags & INO_BLK) && node->check_and_mount) {
        
            if ((*error = node->check_and_mount (node))) {
            
                if (*error > 0) {
                    *error = -EIO;
                }
                
                spin_unlock (&tmp_refcount_lock);
                return 0;
            
            }
            
            *error = -ENOENT;
        
        }
        
        if (node->kopen_flags || node->kopen) {
        
            if (node->kopen_flags) {
                node = node->kopen_flags (node, path, depth, flags, error);
            } else {
                node = node->kopen (node, path, depth);
            }
            
            if (!node) {
            
                spin_unlock (&tmp_refcount_lock);
                return 0;
            
            }
            
            if (!node->creation_time) {
                node->creation_time = creation_time;
            }
            
            if (node->refcount >= 0) {
                node->refcount++;
            }
            
            spin_unlock (&tmp_refcount_lock);
            return node;
        
        }
        
        if (depth) {
        
            *error = -ENOTDIR;
            
            spin_unlock (&tmp_refcount_lock);
            return 0;
        
        }
        
        if (node->refcount >= 0) {
            node->refcount++;
        }
        
        spin_unlock (&tmp_refcount_lock);
        return node;
    
    }
    
    if (depth && (node->flags != INO_DIR || !node->kopen)) {
    
        *error = node->flags != INO_DIR ? -ENOTDIR : -ENOENT;
        
        spin_unlock (&tmp_refcount_lock);
        return 0;
    
    }
    
    if (node->kopen) {
    
        if (!(node = node->kopen (node, path, depth))) {
        
            spin_unlock (&tmp_refcount_lock);
            return 0;
        
        }
        
        if (!node->creation_time) {
            node->creation_time = creation_time;
        }
    
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
    
    path[len] = '\0';
    return path + 1;

}

#define     MIN(a, b)                   ((a) < (b) ? (a) : (b))

static uint8_t vfs_dirent_type (struct fs_node *node) {

    if (!node) {
        return DT_DIR;
    }
    
    switch (node->flags) {

        case INO_REG:
        
            return DT_REG;
        
        case INO_DIR:
        
            return DT_DIR;
        
        case INO_CHR:
        
            return DT_CHR;
        
        case INO_BLK:
        
            return DT_BLK;
        
        case INO_FIFO:
        
            return DT_FIFO;
        
        case INO_LNK:
        
            return DT_LNK;
        
        case INO_SOCK:
        
            return DT_SOCK;
        
        default:
        
            return DT_UNKNOWN;

    }

}

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
        de->d_type = DT_DIR;
        
        de->d_ino = 0;
        de->d_off = 1;
        
        return 1;
    
    } else if (index == 1) {
    
        strcpy (de->d_name, "..");
        de->d_type = DT_DIR;
        
        de->d_ino = 1;
        de->d_off = 2;
        
        return 1;
    
    }
    
    index -= 2;
    
    if (d->children) {
    
        for (child = d->children->head; child; child = child->next) {
        
            if (i == index) {
            
                tchild = (struct tree_node *) child->value;
                len = strlen ((n = (struct vfs_entry *) tchild->value)->name) + 1;
                
                memcpy (de->d_name, n->name, MIN (256U, len));
                de->d_type = vfs_dirent_type (n->file);
                
                de->d_off = index + 3;
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
    
    if (node) {
    
        memset (node, 0, sizeof (*node));
        
        node->flags = INO_DIR;
        node->readdir = readdir_mapper;
    
    }
    
    return node;

}

static void panic (const char *s) {

    /* Print the string passed to us. */
    dprintf ("%s\n", s);
    
    /* Halt the system. */
    for (;;) {
    
#if     defined (__AMD64__)
        __asm__ __volatile__ ("pause");
#else
        __asm__ __volatile__ ("hlt");
#endif
    
    }

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

char *vfs_canonicalize_path (const char *path, int *error) {

    struct thread *thread = thread_get_current ();
    const char *cwd = "/";
    
    if (thread && thread->owner && thread->owner->cwd[0]) {
        cwd = thread->owner->cwd;
    }
    
    return canonicalize_path (cwd, path, error);

}

void vfs_close (struct fs_node *node) {
    close_fs (node);
}

void *vfs_mount (const char *path, struct fs_node *local_root) {

    struct tree_node *ret_val = 0, *root_node;
    
    char *p, *i;
    uint64_t seconds;
    
    int error;
    size_t path_len;
    
    if (!fs_tree) {
    
        panic ("VFS hasn't been initialized, you can't mount things yet.\n");
        return 0;
    
    }
    
    if (!(p = canonicalize_path ("/", path, &error))) {
        return 0;
    }
    
    if (local_root) {
    
        if (!local_root->creation_time && cmos_get_unix_time (&seconds)) {
            local_root->creation_time = (int64_t) seconds;
        }
            
        if (!local_root->creation_time) {
            local_root->creation_time = vfs_last_creation_time;
        } else {
            vfs_last_creation_time = local_root->creation_time;
        }
    
    }
    
    path_len = strlen (p);
    
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
            dprintf ("Path %s already mounted, unmount before trying to mount something else.\n", path);
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
            
                if (!(ent = kmalloc (sizeof (*ent)))) {
                    return 0;
                }
                
                memset (ent, 0, sizeof (*ent));
                ent->name = kstrdup (at);
                
                node = tree_node_insert_child (fs_tree, node, ent);
            
            }
            
            at = at + strlen (at) + 1;
        
        }
        
        ent = (struct vfs_entry *) node->value;
        
        if (ent->file) {
            dprintf ("Path %s already mounted, unmount before trying to mount something else.\n", path);
        }
        
        ent->file = local_root;
        ret_val = node;
    
    }
    
    kfree (p);
    return ret_val;

}

static struct fs_node *vfs_open_internal (const char *path, int metadata, int flags, int *error) {

    struct fs_node *node;
    
    char *path_offset, *temp_path;
    size_t path_depth = 0, depth = 0, path_len;
    
    *error = -ENOENT;
    
    if (!path) {
    
        *error = -EFAULT;
        return 0;
    
    }
    
    if (!*path) {
        return 0;
    }
    
    if (!(temp_path = vfs_canonicalize_path (path, error))) {
        return 0;
    }
    
    path_len = strlen (temp_path);
    
    if (path_len > 1) {
    
        path_offset = path_tokenize (temp_path, path_len, &path_depth);
        
        if (!(node = get_mount_point (temp_path, path_depth, &path_offset, &depth))) {
        
            kfree (temp_path);
            return 0;
        
        }
    
    } else {
    
        path_offset = temp_path + 1;
        node = fs_root;
    
    }
    
    if (metadata && path_len > 1 && path_depth == depth && node->flags == INO_BLK) {
        vfs_retain (node);
    } else {
        node = open_fs (node, path_offset, path_depth - depth, flags, error);
    }
    
    if (node && path_len == 1 && node->flags == INO_DIR) {
        node->vfs_overlay = fs_tree->root;
    }
    
    kfree (temp_path);
    return node;

}

struct fs_node *vfs_open (const char *path) {

    int error;
    return vfs_open_internal (path, 0, VFS_O_RDONLY, &error);

}

struct fs_node *vfs_open_flags (const char *path, int flags, int *error) {
    return vfs_open_internal (path, 0, flags, error);
}

struct fs_node *vfs_open_for_stat (const char *path, int *error) {
    return vfs_open_internal (path, 1, VFS_O_RDONLY, error);
}

int vfs_mkdir (const char *path, mode_t mode) {

    struct fs_node *node;
    char *path_offset, *temp_path;
    
    size_t path_depth = 0, depth = 0, path_len;
    int ret = -ENOENT;
    
    if (!path) {
        return -EFAULT;
    }
    
    if (!*path) {
        return -ENOENT;
    }
    
    if (!(temp_path = vfs_canonicalize_path (path, &ret))) {
        return ret;
    }
    
    if ((path_len = strlen (temp_path)) == 1) {
    
        kfree (temp_path);
        return -EEXIST;
    
    }
    
    path_offset = path_tokenize (temp_path, path_len, &path_depth);
    
    if (!(node = get_mount_point (temp_path, path_depth, &path_offset, &depth))) {
    
        kfree (temp_path);
        return -ENOENT;
    
    }
    
    if (path_depth == depth) {
    
        kfree (temp_path);
        return -EEXIST;
    
    }
    
    spin_lock (&tmp_refcount_lock);
    
    if (node->check_and_mount && (ret = node->check_and_mount (node))) {
        ret = ret < 0 ? ret : -EIO;
    } else if (node->kmkdir) {
        ret = node->kmkdir (node, path_offset, path_depth - depth, mode);
    } else {
        ret = (node->flags == INO_DIR || (node->flags & INO_BLK)) ? -EROFS : -ENOTDIR;
    }
    
    spin_unlock (&tmp_refcount_lock);
    kfree (temp_path);
    
    return ret;

}

int vfs_read (struct fs_node *node, void *buf, size_t count) {

    int ret = node->flags == INO_DIR ? -EISDIR : -EBADF;
    spin_lock (&tmp_read_lock);
    
    if (node->flags != INO_DIR && node->kread) {
        ret = node->kread (node, buf, count);
    }
    
    spin_unlock (&tmp_read_lock);
    return ret;

}

int vfs_readdir (struct fs_node *node, uint32_t index, struct dirent *de) {

    struct tree_node *d = (struct tree_node *) node->vfs_overlay;
    
    struct tree_node *tchild;
    struct vfs_entry *n;
    
    struct list_node *child;
    uint32_t virtual_count = 0, i;
    
    size_t len;
    int ret;
    
    if (!d || !d->children) {
        return node->readdir (node, index, de);
    }
    
    if (index < 2) {
        return node->readdir (node, index, de);
    }
    
    for (child = d->children->head; child; child = child->next) {
        virtual_count++;
    }
    
    if (index - 2 < virtual_count) {
    
        for (child = d->children->head, i = 0; child; child = child->next, i++) {
        
            if (i == index - 2) {
            
                tchild = (struct tree_node *) child->value;
                n = (struct vfs_entry *) tchild->value;
                
                len = strlen (n->name);
                len = MIN (len, sizeof (de->d_name) - 1);
                
                memcpy (de->d_name, n->name, len);
                de->d_type = vfs_dirent_type (n->file);
                
                de->d_name[len] = '\0';
                de->d_ino = index;
                de->d_off = index + 1;
                
                return 1;
            
            }
        
        }
    
        return 0;
    
    }
    
    index -= virtual_count;
    
    for (;;) {
    
        ret = node->readdir (node, index, de);
        
        if (ret <= 0) {
            return ret;
        }
        
        for (child = d->children->head; child; child = child->next) {
        
            tchild = (struct tree_node *) child->value;
            n = (struct vfs_entry *) tchild->value;
            
            if (!strcmp (n->name, de->d_name)) {
                break;
            }
        
        }
        
        if (!child) {
            de->d_off += virtual_count;
            return 1;
        }
        
        index = (uint32_t) de->d_off;
    
    }

}

int vfs_remove (const char *path, int directory) {

    struct fs_node *node;
    char *path_offset, *temp_path;
    
    size_t path_depth = 0, depth = 0, path_len, end, start;
    int ret = -ENOENT;
    
    if (!path) {
        return -EFAULT;
    }
    
    if (!*path) {
        return -ENOENT;
    }
    
    end = strlen (path);
    start = end;
    
    while (end && (path[end - 1] == '/' || path[end - 1] == '\\')) {
        end--;
    }
    
    if (!directory && end != start) {
        directory = 2;
    }
    
    start = end;
    
    while (start && path[start - 1] != '/' && path[start - 1] != '\\') {
        start--;
    }
    
    if ((end - start == 1 && path[start] == '.') || (end - start == 2 && path[start] == '.' && path[start + 1] == '.')) {
        return directory == 1 ? -EINVAL : -EISDIR;
    }
    
    if (!(temp_path = vfs_canonicalize_path (path, &ret))) {
        return ret;
    }
    
    if ((path_len = strlen (temp_path)) == 1) {
    
        kfree (temp_path);
        return -EBUSY;
    
    }
    
    path_offset = path_tokenize (temp_path, path_len, &path_depth);
    
    if (!(node = get_mount_point (temp_path, path_depth, &path_offset, &depth))) {
    
        kfree (temp_path);
        return -ENOENT;
    
    }
    
    if (path_depth == depth) {
    
        kfree (temp_path);
        return -EBUSY;
    
    }
    
    spin_lock (&tmp_refcount_lock);
    
    if (node->check_and_mount && (ret = node->check_and_mount (node))) {
        ret = ret < 0 ? ret : -EIO;
    } else if (node->kremove) {
        ret = node->kremove (node, path_offset, path_depth - depth, directory);
    } else {
        ret = (node->flags == INO_DIR || (node->flags & INO_BLK)) ? -EROFS : -ENOTDIR;
    }
    
    spin_unlock (&tmp_refcount_lock);
    kfree (temp_path);
    
    return ret;

}

int vfs_rename (const char *old_path, const char *new_path) {

    struct fs_node *old_node, *new_node;
    char *old_temp, *new_temp, *old_offset, *new_offset;
    
    const char *path;
    
    size_t old_depth = 0, new_depth = 0, old_mount = 0, new_mount = 0;
    size_t old_len, new_len, end, start;
    
    int ret = -ENOENT, i, directory = 0;
    
    if (!old_path || !new_path) {
        return -EFAULT;
    }
    
    if (!*old_path || !*new_path) {
        return -ENOENT;
    }
    
    for (i = 0; i < 2; i++) {
    
        path = i ? new_path : old_path;
        end = strlen (path);
        
        start = end;
        
        while (end && (path[end - 1] == '/' || path[end - 1] == '\\')) {
            end--;
        }
        
        if (end != start) {
            directory = 1;
        }
        
        start = end;
        
        while (start && path[start - 1] != '/' && path[start - 1] != '\\') {
            start--;
        }
        
        if ((end - start == 1 && path[start] == '.') || (end - start == 2 && path[start] == '.' && path[start + 1] == '.')) {
            return -EINVAL;
        }
    
    }
    
    if (!(old_temp = vfs_canonicalize_path (old_path, &ret))) {
        return ret;
    }
    
    if (!(new_temp = vfs_canonicalize_path (new_path, &ret))) {
    
        kfree (old_temp);
        return ret;
    
    }
    
    old_len = strlen (old_temp);
    new_len = strlen (new_temp);
    
    if (old_len == 1 || new_len == 1) {
    
        ret = -EBUSY;
        goto done;
    
    }
    
    old_offset = path_tokenize (old_temp, old_len, &old_depth);
    new_offset = path_tokenize (new_temp, new_len, &new_depth);
    
    old_node = get_mount_point (old_temp, old_depth, &old_offset, &old_mount);
    new_node = get_mount_point (new_temp, new_depth, &new_offset, &new_mount);
    
    if (!old_node || !new_node) {
    
        ret = -ENOENT;
        goto done;
    
    }
    
    if (old_depth == old_mount || new_depth == new_mount) {
    
        ret = -EBUSY;
        goto done;
    
    }
    
    if (old_node != new_node) {
    
        ret = -EXDEV;
        goto done;
    
    }
    
    spin_lock (&tmp_refcount_lock);
    
    if (old_node->check_and_mount && (ret = old_node->check_and_mount (old_node))) {
        ret = ret < 0 ? ret : -EIO;
    } else if (old_node->krename) {
        ret = old_node->krename (old_node, old_offset, old_depth - old_mount, new_offset, new_depth - new_mount, directory);
    } else {
        ret = (old_node->flags == INO_DIR || (old_node->flags & INO_BLK)) ? -EROFS : -ENOTDIR;
    }
    
    spin_unlock (&tmp_refcount_lock);

done:

    kfree (old_temp);
    kfree (new_temp);
    
    return ret;

}

void vfs_retain (struct fs_node *node) {

    if (!node) {
        return;
    }
    
    spin_lock (&tmp_refcount_lock);
    
    if (node->refcount >= 0) {
        node->refcount++;
    }
    
    spin_unlock (&tmp_refcount_lock);

}

int vfs_root_registered (void) {

    struct tree_node *root_node;
    struct vfs_entry *root;
    
    root_node = fs_tree->root;
    
    root = (struct vfs_entry *) root_node->value;
    return root->file != 0;

}

off_t vfs_seek (struct fs_node *node, off_t offset, int whence) {

    off_t ret = -ESPIPE;
    spin_lock (&tmp_seek_lock);
    
    if (node->flags != INO_DIR && node->kseek) {
        ret = node->kseek (node, offset, whence);
    }
    
    spin_unlock (&tmp_seek_lock);
    return ret;

}

int vfs_stat (struct fs_node *node, struct stat *st) {

    int ret;
    
    if (!node || !st) {
        return -EFAULT;
    }
    
    memset (st, 0, sizeof (*st));
    
    switch (node->flags) {
    
        case INO_REG:
        
            st->st_mode = S_IFREG | 0644;
            break;
        
        case INO_DIR:
        
            st->st_mode = S_IFDIR | 0755;
            break;
        
        case INO_CHR:
        
            st->st_mode = S_IFCHR | 0666;
            break;
        
        case INO_BLK:
        
            st->st_mode = S_IFBLK | 0660;
            break;
        
        case INO_FIFO:
        
            st->st_mode = S_IFIFO | 0644;
            break;
        
        case INO_LNK:
        
            st->st_mode = S_IFLNK | 0777;
            break;
        
        case INO_SOCK:
        
            st->st_mode = S_IFSOCK | 0666;
            break;
        
        default:
        
            return -EINVAL;
    
    }
    
    st->st_nlink = node->flags == INO_DIR ? 2 : 1;
    st->st_atime = node->creation_time;
    st->st_mtime = node->creation_time;
    st->st_ctime = node->creation_time;
    
    if (node->kstat) {
    
        ret = node->kstat (node, st);
        
        if (ret < 0) {
            return ret;
        }
        
        if (!st->st_mtime && node->creation_time) {
        
            st->st_atime = node->creation_time;
            st->st_mtime = node->creation_time;
            st->st_ctime = node->creation_time;
        
        }
    
    }
    
    return node;

}

int vfs_statfs (struct fs_node *node, struct vfs_statfs *st) {
    
    if (!node || !st) {
        return -EFAULT;
    }
    
    if (!node->kstatfs) {
        return -ENOSYS;
    }
    
    memset (st, 0, sizeof (*st));
    return node->kstatfs (node, st);

}

int vfs_truncate (struct fs_node *node, off_t length) {

    int ret = -EINVAL;
    
    if (length < 0) {
        return -EINVAL;
    }
    
    if (node->flags == INO_DIR) {
        return -EISDIR;
    }
    
    if (node->flags != INO_REG) {
        return -EINVAL;
    }
    
    spin_lock (&tmp_write_lock);
    
    if (node->ktruncate) {
        ret = node->ktruncate (node, length);
    } else if (!node->kwrite) {
        ret = -EBADF;
    }
    
    spin_unlock (&tmp_write_lock);
    return ret;

}

int vfs_write (struct fs_node *node, void *buf, size_t count) {

    int ret = node->flags == INO_DIR ? -EISDIR : -EBADF;
    spin_lock (&tmp_write_lock);
    
    if (node->flags != INO_DIR && node->kwrite) {
        ret = node->kwrite (node, buf, count);
    }
    
    spin_unlock (&tmp_write_lock);
    return ret;

}


void debug_print_vfs_tree (void) {
    debug_print_vfs_tree_node (fs_tree->root, 0);
}

void vfs_install (void) {

    struct vfs_entry *root;
    fs_tree = tree_create ();
    
    if ((root = kmalloc (sizeof (*root)))) {
    
        memset (root, 0, sizeof (*root));
        
        root->name = kstrdup ("[root]");
        tree_set_root (fs_tree, root);
    
    }

}

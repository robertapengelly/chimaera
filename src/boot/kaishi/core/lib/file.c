/******************************************************************************
 * @file            file.c
 *****************************************************************************/
#include    <kaishi/vfs/node.h>
#include    <kaishi/vfs/vfs.h>

#include    <stddef.h>

struct fs_node *kopen (const char *path) {
    return vfs_open (path);
}

int kread (struct fs_node *node, void *buf, size_t count) {
    return vfs_read (node, buf, count);
}

void kclose (struct fs_node *node) {
    vfs_close (node);
}

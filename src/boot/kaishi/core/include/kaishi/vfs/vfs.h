/******************************************************************************
 * @file            vfs.h
 *****************************************************************************/
#ifndef     _VFS_H
#define     _VFS_H

#include    <kaishi/vfs/node.h>
#include    <stddef.h>

struct vfs_entry {

    char *name;
    struct fs_node *file;

};

#define     VFS_FILE                    0x01
#define     VFS_DIRECTORY               0x02

struct fs_node *vfs_open (const char *path);
void *vfs_mount (const char *path, struct fs_node *local_root);

uint64_t vfs_get_disk_checksum (void);
void vfs_install (void);

int vfs_read (struct fs_node *node, void *buf, size_t count);
void vfs_close (struct fs_node *node);

void map_vfs_directory (const char *path);
void debug_print_vfs_tree (void);

#endif      /* _VFS_H */

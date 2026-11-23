/******************************************************************************
 * @file            vfs.h
 *****************************************************************************/
#ifndef     _VFS_H
#define     _VFS_H

#include    <corazon/vfs/node.h>

#include    <sys/stat.h>
#include    <stddef.h>

struct vfs_entry {

    char *name;
    struct fs_node *file;

};

struct fs_node *vfs_open_for_stat (const char *path, int *error);
struct fs_node *vfs_open_flags (const char *path, int flags, int *error);
struct fs_node *vfs_open (const char *path);

char *vfs_canonicalize_path (const char *path, int *error);
void *vfs_mount (const char *path, struct fs_node *local_root);

int vfs_readdir (struct fs_node *node, uint32_t index, struct dirent *de);
int vfs_statfs (struct fs_node *node, struct vfs_statfs *st);
int vfs_stat (struct fs_node *node, struct stat *st);

off_t vfs_seek (struct fs_node *node, off_t offset, int whence);
int vfs_root_registered (void);

int vfs_truncate (struct fs_node *node, off_t length);
int vfs_rename (const char *old_path, const char *new_path);

void vfs_install (void);

int vfs_remove (const char *path, int directory);
int vfs_mkdir (const char *path, mode_t mode);

int vfs_read (struct fs_node *node, void *buf, size_t count);
int vfs_write (struct fs_node *node, void *buf, size_t count);

void vfs_close (struct fs_node *node);
void vfs_retain (struct fs_node *node);

void map_vfs_directory (const char *path);
void debug_print_vfs_tree (void);

#endif      /* _VFS_H */

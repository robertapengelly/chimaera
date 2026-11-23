/******************************************************************************
 * @file            node.h
 *****************************************************************************/
#ifndef     _NODE_H
#define     _NODE_H

#include    <dirent.h>
#include    <stddef.h>
#include    <stdint.h>

#define     INO_REG                     1
#define     INO_DIR                     2
#define     INO_CHR                     3
#define     INO_BLK                     4
#define     INO_FIFO                    5
#define     INO_LNK                     6
#define     INO_SOCK                    7
#define     INO_MOUNT                   8

#define     VFS_O_ACCMODE               3
#define     VFS_O_RDONLY                0
#define     VFS_O_WRONLY                1
#define     VFS_O_RDWR                  2

#define     VFS_O_CREAT                 0100
#define     VFS_O_EXCL                  0200
#define     VFS_O_TRUNC                 01000
#define     VFS_O_APPEND                02000

#define     VFS_POLLIN                  1
#define     VFS_POLLOUT                 2

struct vfs_statfs {

    uint64_t type;
    uint64_t bsize;
    uint64_t blocks;
    uint64_t bfree;
    uint64_t bavail;
    uint64_t files;
    uint64_t ffree;
    uint64_t namelen;
    uint64_t frsize;
    uint64_t flags;

};

struct fs_node {

    uint8_t checksum[20];
    
    int refcount;
    uint64_t flags;
    
    struct fs_node *parent;
    int creation_time;
    
    void *device;
    void *data;
    
    struct fs_node *(*kopen) (void *data, const char *path, size_t depth);
    int (*kutime) (void *data, int64_t atime, int64_t mtime);
    
    int (*check_and_mount) (void *data);
    void (*kclose) (void *data);
    
    int (*readdir) (void *data, uint32_t index, struct dirent *de);
    int (*read) (void *data, void *scratch, uint64_t sector, size_t count);
    
    int (*kread) (void *data, void *buf, size_t count);
    int (*kwrite) (void *data, void *buf, size_t count);
    
    int (*kioctl) (void *data, unsigned long request, void *arg);
    int (*kstat) (void *data, struct stat *st);
    
    int (*kstatfs) (void *data, struct vfs_statfs *st);
    int (*kpoll) (void *data);
    
    off_t (*kseek) (void *data, off_t offset, int whence);
    void *vfs_overlay;
    
    int (*write) (void *data, void *scratch, uint64_t sector, size_t count);
    struct fs_node *(*kopen_flags) (void *data, const char *path, size_t depth, int flags, int *error);
    
    int (*kremove) (void *data, const char *path, size_t depth, int directory);
    int (*kmkdir) (void *data, const char *path, size_t depth, mode_t mode);
    
    int (*ktruncate) (void *data, off_t length);
    int (*krename) (void *data, const char *old_path, size_t old_depth, const char *new_path, size_t new_depth, int directory);

};

#endif      /* _NODE_H */

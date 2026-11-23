/******************************************************************************
 * @file            stat.h
 *****************************************************************************/
#ifndef     _SYS_STAT_H
#define     _SYS_STAT_H

#include    <sys/types.h>
#include    <stdint.h>

#define     S_IFMT                      0170000
#define     S_IFIFO                     0010000
#define     S_IFCHR                     0020000
#define     S_IFDIR                     0040000
#define     S_IFBLK                     0060000
#define     S_IFREG                     0100000
#define     S_IFLNK                     0120000
#define     S_IFSOCK                    0140000

#define     S_IWUSR                     0000200
#define     S_IWGRP                     0000020
#define     S_IWOTH                     0000002

#define     S_ISDIR(m)                  (((m) & S_IFMT) == S_IFDIR)
#define     S_ISREG(m)                  (((m) & S_IFMT) == S_IFREG)
#define     S_ISCHR(m)                  (((m) & S_IFMT) == S_IFCHR)
#define     S_ISBLK(m)                  (((m) & S_IFMT) == S_IFBLK)

__attribute__ ((packed)) struct stat {

    uint64_t st_dev;
    uint64_t st_ino;
    
    mode_t st_mode;
    
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    
    uint64_t st_rdev;
    
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime;
    int64_t st_mtime;
    int64_t st_ctime;

};

int mkdir (const char *path, mode_t mode);
int stat (const char *path, struct stat *buf);

int lstat (const char *path, struct stat *buf);
int fstat (int fd, struct stat *buf);

#endif      /* _SYS_STAT_H */

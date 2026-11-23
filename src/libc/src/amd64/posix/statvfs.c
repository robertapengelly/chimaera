/******************************************************************************
 * @file            statvfs.c
 *****************************************************************************/
#include    <sys/statvfs.h>

#include    <errno.h>
#include    <stdint.h>

#if     defined (__AMD64__)
typedef     uint64_t                    statfs_word_t;
#else
typedef     uint32_t                    statfs_word_t;
#endif

__attribute__ ((packed)) struct linux_statfs {

    statfs_word_t f_type;
    statfs_word_t f_bsize;
    statfs_word_t f_blocks;
    statfs_word_t f_bfree;
    statfs_word_t f_bavail;
    statfs_word_t f_files;
    statfs_word_t f_ffree;
    
    int32_t f_fsid[2];
    
    statfs_word_t f_namelen;
    statfs_word_t f_frsize;
    statfs_word_t f_flags;
    statfs_word_t f_spare[4];

};

static void statvfs_convert (struct statvfs *buf, const struct linux_statfs *st) {

    buf->f_bsize = (unsigned long) st->f_bsize;
    buf->f_frsize = (unsigned long) (st->f_frsize ? st->f_frsize : st->f_bsize);
    
    buf->f_blocks = st->f_blocks;
    buf->f_bfree = st->f_bfree;
    buf->f_bavail = st->f_bavail;
    buf->f_files = st->f_files;
    buf->f_ffree = st->f_ffree;
    buf->f_favail = st->f_ffree;
    
    buf->f_fsid = (unsigned long) st->f_fsid[0];
    buf->f_flag = (unsigned long) st->f_flags;
    buf->f_namemax = (unsigned long) st->f_namelen;

}

int statvfs (const char *path, struct statvfs *buf) {

    struct linux_statfs st;
    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x89), "D" (path), "S" (&st) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x63), "b" (path), "c" (&st) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    statvfs_convert (buf, &st);
    return 0;

}

int fstatvfs (int fd, struct statvfs *buf) {

    struct linux_statfs st;
    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0x8A), "D" (fd), "S" (&st) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0x64), "b" (fd), "c" (&st) : "memory");
#endif
    
    if (ret < 0) {
    
        errno = -ret;
        return -1;
    
    }
    
    statvfs_convert (buf, &st);
    return 0;

}

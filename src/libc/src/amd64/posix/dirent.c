/******************************************************************************
 * @file            dirent.c
 *****************************************************************************/
#include    <dirent.h>
#include    <errno.h>
#include    <fcntl.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <unistd.h>

__attribute__ ((packed)) struct linux_dirent64 {

    uint64_t d_ino;
    int64_t d_off;
    
    uint16_t d_reclen;
    uint8_t d_type;
    
    char d_name[1];

};

struct __dirstream {

    int fd;
    
    size_t pos, length;
    struct dirent entry;
    
    uint8_t buffer[1024];

};

static int getdents64 (int fd, void *buffer, size_t size) {

    int ret;
    
#if     defined (__AMD64__)
    __asm__ __volatile__ ("syscall" : "=a" (ret) : "a" (0xD9), "D" (fd), "S" (buffer), "d" (size) : "rcx", "r11", "memory");
#else
    __asm__ __volatile__ ("int 0x80" : "=a" (ret) : "a" (0xDC), "b" (fd), "c" (buffer), "d" (size) : "memory");
#endif
    
    return ret;

}

DIR *opendir (const char *name) {

    int ret, fd;
    DIR *dir;
    
    if (!name) {
    
        errno = EINVAL;
        return 0;
    
    }
    
    if ((fd = open (name, O_RDONLY)) < 0) {
        return 0;
    }
    
    if (!(dir = malloc (sizeof (*dir)))) {
    
        ret = errno;
        close (fd);
        
        errno = ret;
        return 0;
    
    }
    
    dir->fd = fd;
    dir->pos = 0;
    
    ret = getdents64 (fd, dir->buffer, sizeof (dir->buffer));
    
    if (ret < 0) {
    
        close (fd);
        free (dir);
        
        errno = -ret;
        return 0;
    
    }
    
    dir->length = (size_t) ret;
    return dir;

}

struct dirent *readdir (DIR *dirp) {

    struct linux_dirent64 *entry;
    size_t size, length, i;
    
    int ret;
    
    if (!dirp) {
    
        errno = EBADF;
        return 0;
    
    }
    
    if (dirp->pos == dirp->length) {
    
        ret = getdents64 (dirp->fd, dirp->buffer, sizeof (dirp->buffer));
        
        if (ret < 0) {
        
            errno = -ret;
            return 0;
        
        }
        
        if (!ret) {
            return 0;
        }
        
        dirp->length = (size_t) ret;
        dirp->pos = 0;
    
    }
    
    entry = (struct linux_dirent64 *) (dirp->buffer + dirp->pos);
    size = entry->d_reclen;
    
    if (size < 20 || size > dirp->length - dirp->pos) {
    
        errno = EIO;
        return 0;
    
    }
    
    length = size - 19;
    
    if (length > sizeof (dirp->entry.d_name) - 1) {
        length = sizeof (dirp->entry.d_name) - 1;
    }
    
    dirp->entry.d_ino = (ino_t) entry->d_ino;
    dirp->entry.d_off = (off_t) entry->d_off;
    
    dirp->entry.d_reclen = entry->d_reclen;
    dirp->entry.d_type = entry->d_type;
    
    for (i = 0; i < length && entry->d_name[i]; i++) {
        dirp->entry.d_name[i] = entry->d_name[i];
    }
    
    dirp->entry.d_name[i] = '\0';
    dirp->pos += size;
    
    return &dirp->entry;

}

void rewinddir (DIR *dirp) {

    if (!dirp || lseek (dirp->fd, 0, SEEK_SET) < 0) {
        return;
    }
    
    dirp->pos = 0;
    dirp->length = 0;

}

int closedir (DIR *dirp) {

    int ret;
    
    if (!dirp) {
    
        errno = EBADF;
        return -1;
    
    }
    
    ret = close (dirp->fd);
    
    free (dirp);
    return ret;

}

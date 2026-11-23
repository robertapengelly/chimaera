/******************************************************************************
 * @file            dirent.h
 *****************************************************************************/
#ifndef     _DIRENT_H
#define     _DIRENT_H

#include    <sys/types.h>
#include    <stdint.h>

typedef struct dirent {

    ino_t d_ino;
    off_t d_off;
    
    uint16_t d_reclen;
    uint8_t d_type;
    
    char d_name[256];

} dirent_t;

#define     DT_UNKNOWN                  0x00
#define     DT_FIFO                     0x01
#define     DT_CHR                      0x02
#define     DT_DIR                      0x04
#define     DT_BLK                      0x06
#define     DT_REG                      0x08
#define     DT_LNK                      0x0A
#define     DT_SOCK                     0x0C

struct __dirstream;
typedef     struct __dirstream          DIR;

struct dirent *readdir (DIR *dirp);
DIR *opendir (const char *name);

void rewinddir (DIR *dirp);
int closedir (DIR *dirp);

#endif      /* _DIRENT_H */

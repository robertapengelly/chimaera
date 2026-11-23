/******************************************************************************
 * @file            dirent.h
 *****************************************************************************/
#ifndef     _DIRENT_H
#define     _DIRENT_H

#include    <sys/types.h>

typedef struct dirent {

    ino_t d_ino;
    char d_name[256];

};

#endif      /* _DIRENT_H */

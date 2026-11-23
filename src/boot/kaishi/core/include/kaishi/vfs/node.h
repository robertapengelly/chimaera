/******************************************************************************
 * @file            node.h
 *****************************************************************************/
#ifndef     _NODE_H
#define     _NODE_H

#include    <dirent.h>
#include    <stddef.h>
#include    <stdint.h>

struct fs_node {

    uint64_t checksum_hash;
    
    int refcount;
    uint64_t flags;
    
    struct fs_node *parent;
    
    void *device;
    void *data;
    
    struct fs_node *(*kopen) (void *data, const char *path, size_t depth);
    int (*check_and_mount) (void *data);
    
    int (*readdir) (void *data, uint32_t index, struct dirent *de);
    int (*read) (void *data, void *scratch, uint64_t sector, size_t count);
    
    int (*kclose) (void *data);
    int (*kread) (void *data, void *buf, size_t count);

};

#endif      /* _NODE_H */

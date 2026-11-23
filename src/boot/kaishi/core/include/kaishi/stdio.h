/******************************************************************************
 * @file            stdio.h
 *****************************************************************************/
#ifndef     _KAISHI_STDIO_H
#define     _KAISHI_STDIO_H

#include    <kaishi/vfs/node.h>
#include    <stddef.h>

struct fs_node *kopen (const char *path);

int kread (struct fs_node *node, void *buf, size_t count);
int kprintf (const char *fmt, ...);

void kclose (struct fs_node *node);
void panic (const char *fmt, ...);

#endif      /* _KAISHI_STDIO_H */

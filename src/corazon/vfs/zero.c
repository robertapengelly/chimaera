/******************************************************************************
 * @file            zero.c
 *****************************************************************************/
#include    <corazon/utils/misc.h>
#include    <corazon/vfs/node.h>
#include    <corazon/vfs/vfs.h>

#include    <string.h>

static int read_zero (void *data, void *buf, size_t count) {

    (void) data;
    
    memset (buf, 0, count);
    return count;

}

static int write_zero (void *data, void *buf, size_t count) {

    (void) data;
    (void) buf;
    
    return count;

}

static void create_null (void) {

    struct fs_node *node = 0;
    
    if (node = kmalloc (sizeof (*node))) {
    
        memset (node, 0, sizeof (*node));
        
        node->flags  = INO_CHR;
        node->kwrite = write_zero;
        
        vfs_mount ("/dev/null", node);
    
    }

}

static void create_zero (void) {

    struct fs_node *node = 0;
    
    if (node = kmalloc (sizeof (*node))) {
    
        memset (node, 0, sizeof (*node));
        
        node->flags  = INO_CHR;
        node->kread  = read_zero;
        node->kwrite = write_zero;
        
        vfs_mount ("/dev/zero", node);
    
    }

}

void zero_install (void) {

    create_null ();
    create_zero ();

}

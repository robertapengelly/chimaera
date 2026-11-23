/******************************************************************************
 * @file            msdos.c
 *****************************************************************************/
#include    <kaishi/vfs/ext2.h>
#include    <kaishi/vfs/node.h>
#include    <kaishi/misc.h>
#include    <kaishi/stdio.h>
#include    <kaishi/stdlib.h>

#include    <stdint.h>
#include    <string.h>

static uint32_t inode_size = 0;
static uint32_t inodes_count = 0;

static uint32_t blocks_count = 0;
static uint32_t blocks_per_group = 0;

static uint64_t pointers_per_block = 0;
static uint64_t block_size = 0;
static uint64_t inodes_per_group = 0;

void *ext2_probe (void *data, void *scratch) {

    struct fs_node *node = 0, *root_node = 0;
    
    struct ext2_params *this = 0;
    struct ext2_sb *sb = 0;
    
    uint32_t tmp;
    
    if (!(root_node = (struct fs_node *) data)) {
        return 0;
    }
    
    sb = (struct ext2_sb *) scratch;
    
    if (sb->s_magic[0] != (EXT2_SUPER_MAGIC & 0xff) || sb->s_magic[1] != ((EXT2_SUPER_MAGIC >> 8) & 0xff)) {
        return 0;
    }
    
    if (!(inode_size = (uint32_t) array_to_int (sb->s_inode_size, 2))) {
        inode_size = 128;
    }
    
    block_size = 1024 << (uint32_t) array_to_int (sb->s_log_block_size, 4);
    pointers_per_block = block_size / 4;
    
    blocks_count = (uint32_t) array_to_int (sb->s_blocks_count, 4);
    blocks_per_group = (uint32_t) array_to_int (sb->s_blocks_per_group, 4);
    
    tmp = blocks_count / blocks_per_group;
    
    if ((blocks_per_group * tmp) < blocks_count) {
        tmp++;
    }
    
    inodes_count = (uint32_t) array_to_int (sb->s_inodes_count, 4);
    inodes_per_group = inodes_count / tmp;
    
    this = kmalloc (sizeof (*this));
    
    this->blocks_count = blocks_count;
    this->blocks_per_group = blocks_per_group;
    
    this->inode_size = inode_size;
    this->inodes_count = inodes_count;
    
    this->block_size = block_size;
    this->pointers_per_block = pointers_per_block;
    
    this->inodes_per_group = inodes_per_group;
    
    node = kmalloc (sizeof (*node));
    
    node->parent = root_node;
    node->data = this;
    
    return node;

}

/******************************************************************************
 * @file            ext2.h
 *****************************************************************************/
#ifndef     _EXT2_H
#define     _EXT2_H

#include    <stdint.h>

struct ext2_sb {

    uint8_t s_inodes_count[4];
    uint8_t s_blocks_count[4];
    
    uint8_t s_r_blocks_count[4];
    
    uint8_t s_free_blocks_count[4];
    uint8_t s_free_inodes_count[4];
    
    uint8_t s_first_data_block[4];
    
    uint8_t s_log_block_size[4];
    uint8_t s_log_frag_size[4];
    
    uint8_t s_blocks_per_group[4];
    uint8_t s_frags_per_group[4];
    uint8_t s_inodes_per_group[4];
    
    uint8_t s_mtime[4];
    uint8_t s_wtime[4];
    
    uint8_t s_mount_count[2];
    uint8_t s_max_mount_count[2];
    
    uint8_t s_magic[2];
    uint8_t s_state[2];
    uint8_t s_errors[2];
    
    uint8_t s_minor_rev_level[2];
    
    uint8_t s_lastcheck[4];
    uint8_t s_checkinterval[4];
    uint8_t s_creator_os[4];
    uint8_t s_rev_level[4];
    
    uint8_t s_def_resuid[2];
    uint8_t s_def_resgid[2];
    
    uint8_t s_first_ino[4];
    uint8_t s_inode_size[2];
    uint8_t s_block_group_nr[2];
    
    uint8_t s_feature_compat[4];
    uint8_t s_feature_incompat[4];
    uint8_t s_feature_ro_compat[4];
    
    uint8_t s_uuid[16];
    uint8_t s_volume_label[16];
    uint8_t s_last_mounted[64];
    
    uint8_t s_algorithm_usage_bitmap[4];
    uint8_t s_reserved[820];

};

#define     EXT2_SUPER_MAGIC            0xEF53

struct ext2_params {

    uint32_t inode_size;
    uint32_t inodes_count;

    uint32_t blocks_count;
    uint32_t blocks_per_group;

    uint64_t pointers_per_block;
    uint64_t block_size;
    
    uint32_t inodes_per_group;

};

void *ext2_probe (void *data, void *scratch);

#endif      /* _EXT2_H*/

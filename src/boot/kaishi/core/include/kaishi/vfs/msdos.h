/******************************************************************************
 * @file            msdos.h
 *****************************************************************************/
#ifndef     _MSDOS_H
#define     _MSDOS_H

#include    <stdint.h>

struct msdos_volume_info {

    uint8_t drive_no;
    uint8_t boot_flags;
    uint8_t ext_boot_sign;
    uint8_t volume_id[4];
    uint8_t volume_label[11];
    uint8_t fs_type[8];

};

struct msdos_boot_sector {

    uint8_t boot_jump[3];
    uint8_t system_id[8];
    uint8_t bytes_per_sector[2];
    uint8_t sectors_per_cluster;
    uint8_t reserved_sectors[2];
    uint8_t no_fats;
    uint8_t root_entries[2];
    uint8_t total_sectors16[2];
    uint8_t media_descriptor;
    uint8_t sectors_per_fat16[2];
    uint8_t sectors_per_track[2];
    uint8_t heads_per_cylinder[2];
    uint8_t hidden_sectors[4];
    uint8_t total_sectors32[4];
    
    union {
    
        struct {
        
            struct msdos_volume_info vi;
            uint8_t boot_code[448];
        
        } _oldfat;
        
        struct {
        
            uint8_t sectors_per_fat32[4];
            uint8_t flags[2];
            uint8_t version[2];
            uint8_t root_cluster[4];
            uint8_t info_sector[2];
            uint8_t backup_boot[2];
            uint16_t reserved2[6];
            
            struct msdos_volume_info vi;
            uint8_t boot_code[420];
        
        } _fat32;
    
    } fstype;
    
    uint8_t boot_sign[2];

};

#define     oldfat                      fstype._oldfat
#define     fat32                       fstype._fat32

struct fat32_fsinfo {

    uint8_t reserved1[4];
    uint8_t signature[4];
    uint8_t free_clusters[4];
    uint8_t next_cluster[4];

};

/**
 * According to Microsoft FAT specification (fatgen103.doc) disk with
 * 4085 clusters (or more) is FAT16, but Microsoft Windows FAT driver
 * fastfat.sys detects disk with less then 4087 clusters as FAT12.
 * Linux FAT drivers msdos.ko and vfat.ko detect disk with at least
 * 4085 clusters as FAT16, therefore for compatibility reasons with
 * both systems disallow formatting disks to 4085 or 4086 clusters.
 */
#define     MAX_CLUST_12                4084
#define     MIN_CLUST_16                4087

/**
 * According to Microsoft FAT specification (fatgen103.doc) disk with
 * 65525 clusters (or more) is FAT32, but Microsoft Windows FAT driver
 * fastfat.sys, Linux FAT drivers msdos.ko and vfat.ko detect disk as
 * FAT32 when Sectors Per FAT (fat_length) is set to zero. And not by
 * number of clusters. Still there is cluster upper limit for FAT16.
 */
#define     MAX_CLUST_16                65524
#define     MIN_CLUST_32                65525

/**
 * M$ says the high 4 bits of a FAT32 FAT entry are reserved and don't belong
 * to the cluster number. So the max. cluster# is based on 2^28
 */
#define     MAX_CLUST_32                268435446

/** File Attributes. */
#define     ATTR_NONE                   0x00
#define     ATTR_READONLY               0x01
#define     ATTR_HIDDEN                 0x02
#define     ATTR_SYSTEM                 0x04
#define     ATTR_VOLUME_ID              0x08
#define     ATTR_DIR                    0x10
#define     ATTR_ARCHIVE                0x20

#define     ATTR_UNIX_LINK              0x40
#define     ATTR_UNIX_MODE              0x80

#define     ATTR_LONG_NAME              (ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

struct msdos_specs {

    uint8_t sectors_per_cluster;
    
    uint32_t root_entries;
    uint32_t root_start;
    
    uint32_t reserved_sectors;
    uint32_t data_area;
    
    uint32_t bytes_per_sector;
    uint32_t hidden_sectors;
    
    uint32_t cluster_count;
    uint32_t root_cluster;

};

void *msdos_probe (void *data, void *scratch);

struct msdos_dirent {

    uint8_t name[11];
    uint8_t attr;
    uint8_t type;
    uint8_t csum;
    uint8_t xctime[2];
    uint8_t cdate[2];
    uint8_t adate[2];
    uint8_t starthi[2];
    uint8_t xtime[2];
    uint8_t date[2];
    uint8_t startlo[2];
    uint8_t size[4];

};

#endif      /* _MSDOS_H */

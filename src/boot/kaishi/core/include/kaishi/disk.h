/******************************************************************************
 * @file            disk.h
 *****************************************************************************/
#ifndef     _DISK_H
#define     _DISK_H

#if     defined (UEFI)
# include       <kaishi/efi.h>

__attribute__ ((packed)) typedef struct disk {

    EFI_HANDLE efi_handle;
    EFI_HANDLE part_handle;
    
    EFI_BLOCK_IO *block_io;
    
    UINT16 heads_per_cylinder;
    UINT16 sectors_per_track;
    
    UINT32 total_sectors;
    UINT32 sector_size;
    
    UINT64 no_cylinders;

} disk_t;

void disk_create_index (EFI_HANDLE ImageHandle);
#else
# include       <stdint.h>

__attribute__ ((packed)) typedef struct disk {

    uint8_t edd_available;
    uint8_t drive_no;
    
    uint16_t heads_per_cylinder;
    uint16_t sectors_per_track;
    
    uint32_t total_sectors;
    uint32_t sector_size;
    
    uint64_t no_cylinders;

} disk_t;

void disk_create_index (void);
#endif

uint32_t disk_get_sector_size (void *data);

#endif      /* _DISK_H */
